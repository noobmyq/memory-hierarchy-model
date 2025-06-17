#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "common.h"
#include "data_cache.h"
#include "page_table.h"
#include "pin.H"

using std::cerr;
using std::cout;

// Extended MEMREF structure with private page flag
struct MemrefExtended {
    ADDRINT pc;
    ADDRINT ea;
    UINT64 size;
    bool read;
    bool
        isPrivatePage;  // New field: true if access is to a private page region
};

static_assert(sizeof(MemrefExtended) == 32,
              "MemrefExtended struct has unexpected padding");

// --- Global instruction counter and threshold knob ---
UINT64 gInstrCount(0);
KNOB<UINT64> KnobInstrThreshold(
    KNOB_MODE_WRITEONCE, "pintool", "instr_threshold", "0",
    "Terminate after this many instructions (0 = unlimited)");

// --- Pin Configuration ---
#define NUM_BUF_PAGES 1024
#define VMA_CACHE_FALLBACK_INTERVAL \
    5000  // Fallback update every 5000 batches (much longer)
#define VMA_CACHE_UPDATE_THROTTLE \
    10  // Minimum batches between cache miss updates
const UINT64 kNumBufPages = 1024;
const UINT64 kVmaCacheFallbackInterval = 5000;
const UINT64 kVmaCacheUpdateThrottle = 10;
BUFFER_ID bufId;

// --- Knobs for Simulator Configuration ---
KNOB<UINT64> KnobPhysMemGB(KNOB_MODE_WRITEONCE, "pintool", "phys_mem_gb", "1",
                           "Physical memory size in GB");
KNOB<UINT64> KnobL1TLBSize(KNOB_MODE_WRITEONCE, "pintool", "l1_tlb_size", "64",
                           "L1 TLB size");
KNOB<UINT64> KnobL1TLBWays(KNOB_MODE_WRITEONCE, "pintool", "l1_tlb_ways", "4",
                           "L1 TLB associativity");
KNOB<UINT64> KnobL2TLBSize(KNOB_MODE_WRITEONCE, "pintool", "l2_tlb_size",
                           "1024", "L2 TLB size");
KNOB<UINT64> KnobL2TLBWays(KNOB_MODE_WRITEONCE, "pintool", "l2_tlb_ways", "8",
                           "L2 TLB associativity");
KNOB<UINT64> KnobPGDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pgd_pwc_size",
                            "16", "PWC size");
KNOB<UINT64> KnobPGDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pgd_pwc_ways", "4",
                            "PWC associativity");
KNOB<UINT64> KnobPUDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pud_pwc_size",
                            "16", "PWC size");
KNOB<UINT64> KnobPUDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pud_pwc_ways", "4",
                            "PWC associativity");
KNOB<UINT64> KnobPMDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pmd_pwc_size",
                            "16", "PWC size");
KNOB<UINT64> KnobPMDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pmd_pwc_ways", "4",
                            "PWC associativity");
KNOB<UINT64> KnobL1CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l1_cache_size",
                             "32768", "L1 Cache size in bytes");
KNOB<UINT64> KnobL1Ways(KNOB_MODE_WRITEONCE, "pintool", "l1_ways", "8",
                        "L1 Cache associativity");
KNOB<UINT64> KnobL1Line(KNOB_MODE_WRITEONCE, "pintool", "l1_line", "64",
                        "L1 Cache line size");
KNOB<UINT64> KnobL2CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l2_cache_size",
                             "262144", "L2 Cache size in bytes");
KNOB<UINT64> KnobL2Ways(KNOB_MODE_WRITEONCE, "pintool", "l2_ways", "16",
                        "L2 Cache associativity");
KNOB<UINT64> KnobL2Line(KNOB_MODE_WRITEONCE, "pintool", "l2_line", "64",
                        "L2 Cache line size");
KNOB<UINT64> KnobL3CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l3_cache_size",
                             "8388608", "L3 Cache size in bytes");
KNOB<UINT64> KnobL3Ways(KNOB_MODE_WRITEONCE, "pintool", "l3_ways", "16",
                        "L3 Cache associativity");
KNOB<UINT64> KnobL3Line(KNOB_MODE_WRITEONCE, "pintool", "l3_line", "64",
                        "L3 Cache line size");
KNOB<bool> KnobPteCachable(KNOB_MODE_WRITEONCE, "pintool", "pte_cachable", "0",
                           "PTE cacheable flag");
KNOB<UINT64> KnobPGDSize(KNOB_MODE_WRITEONCE, "pintool", "pgd_size", "512",
                         "Number of PGD entries");
KNOB<UINT64> KnobPUDSize(KNOB_MODE_WRITEONCE, "pintool", "pud_size", "512",
                         "Number of PUD entries");
KNOB<UINT64> KnobPMDSize(KNOB_MODE_WRITEONCE, "pintool", "pmd_size", "512",
                         "Number of PMD entries");
KNOB<UINT64> KnobPTESize(KNOB_MODE_WRITEONCE, "pintool", "pte_size", "512",
                         "Number of PTE entries");
KNOB<bool> KnobTOCEnabled(KNOB_MODE_WRITEONCE, "pintool", "toc_enabled", "0",
                          "Enable Table of Contents (TOC) for PWC");
KNOB<UINT64> KnobTOCSize(KNOB_MODE_WRITEONCE, "pintool", "toc_size", "0",
                         "Size of the Table of Contents (TOC) in bytes");
KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o",
                                 "memory_simulator.out",
                                 "Output file for simulation results");
KNOB<bool> KnobTrackPrivatePages(
    KNOB_MODE_WRITEONCE, "pintool", "track_private", "0",
    "Track private page accesses in memory references");

// --- VMA Region Structure ---
struct VmaRegion {
    ADDRINT startAddr;
    ADDRINT endAddr;
    std::string permissions;
    std::string mapping;
    bool isPrivate;

    VmaRegion(ADDRINT start, ADDRINT end, const std::string& perms,
              const std::string& map, bool priv)
        : startAddr(start),
          endAddr(end),
          permissions(perms),
          mapping(map),
          isPrivate(priv) {}

    bool Contains(ADDRINT addr) const {
        return addr >= startAddr && addr < endAddr;
    }
};

// --- Fast VMA Cache for Private Page Detection ---
class VmaCache {
   public:
    VmaCache()
        : lastUpdateCount_(0),
          cacheMisses_(0),
          cacheHits_(0),
          cacheUpdates_(0) {}

    // Fast lookup: returns true if address is in a private page region
    bool IsPrivatePage(ADDRINT addr) {
        if (!KnobTrackPrivatePages.Value()) {
            return false;  // Feature disabled
        }

        // Binary search through sorted VMA regions
        auto iter =
            std::lower_bound(regions_.begin(), regions_.end(), addr,
                             [](const VmaRegion& region, ADDRINT address) {
                                 return region.endAddr <= address;
                             });

        if (iter != regions_.end() && iter->Contains(addr)) {
            cacheHits_++;
            return iter->isPrivate;
        }

        cacheMisses_++;
        return false;  // Unknown region, assume not private
    }

    // Check if address falls outside all known regions (cache miss)
    bool IsAddressUnknown(ADDRINT addr) {
        if (!KnobTrackPrivatePages.Value()) {
            return false;  // Feature disabled, no cache misses
        }

        auto iter =
            std::lower_bound(regions_.begin(), regions_.end(), addr,
                             [](const VmaRegion& region, ADDRINT address) {
                                 return region.endAddr <= address;
                             });

        return (iter == regions_.end() || !iter->Contains(addr));
    }

    // Update cache from pmap information
    void UpdateFromMaps(OS_PROCESS_ID pid, UINT64 batchCount) {
        if (!KnobTrackPrivatePages.Value()) {
            return;  // Feature disabled
        }

        lastUpdateCount_ = batchCount;
        cacheUpdates_++;
        regions_.clear();

        if (!CollectVmaRegions(pid)) {
            // If pmap -XX fails, disable private page tracking for this update
            return;
        }

        // Sort regions by start address for binary search
        std::sort(regions_.begin(), regions_.end(),
                  [](const VmaRegion& a, const VmaRegion& b) {
                      return a.startAddr < b.startAddr;
                  });

        // Optional: Print map for debugging (remove for production)
        // PrintMap(std::cout);
    }

    // Check if cache needs update - prioritize cache misses over periodic updates
    bool NeedsUpdate(UINT64 batchCount, bool hasUnknownAccess) {
        if (!KnobTrackPrivatePages.Value()) {
            return false;  // Feature disabled, never update
        }

        // Immediate update if we have unknown accesses and enough time has passed since last update
        if (hasUnknownAccess &&
            (batchCount - lastUpdateCount_ >= kVmaCacheUpdateThrottle)) {
            return true;
        }

        // Fallback periodic update (much less frequent)
        return (batchCount - lastUpdateCount_ >= kVmaCacheFallbackInterval);
    }

    void PrintMap(std::ostream& out) {
        out << "VMA Cache Regions (" << regions_.size() << " total):\n";
        for (const auto& region : regions_) {
            out << "  [" << std::hex << region.startAddr << ", "
                << region.endAddr << ") " << std::dec
                << "Perms: " << region.permissions
                << ", Mapping: " << region.mapping
                << ", Private: " << (region.isPrivate ? "Yes" : "No") << "\n";
        }
    }

    void PrintStats(std::ostream& out) {
        out << "VMA Cache Stats (Cache-Miss Triggered Updates):\n";
        out << "  Cache hits: " << cacheHits_ << "\n";
        out << "  Cache misses: " << cacheMisses_ << "\n";
        out << "  Hit ratio: "
            << (cacheHits_ * 100.0 / (cacheHits_ + cacheMisses_)) << "%\n";
        out << "  Cache updates: " << cacheUpdates_ << "\n";
        out << "  Cached regions: " << regions_.size() << "\n";
        out << "  Last update: batch " << lastUpdateCount_ << "\n";
    }

   private:
    std::vector<VmaRegion> regions_;
    UINT64 lastUpdateCount_;
    UINT64 cacheMisses_;
    UINT64 cacheHits_;
    UINT64 cacheUpdates_;

    bool CollectVmaRegions(OS_PROCESS_ID pid) {
        if (!KnobTrackPrivatePages.Value()) {
            return false;  // Feature disabled
        }

        std::string pmapCmd =
            "pmap -XX " + std::to_string(pid) + " 2>/dev/null";

        FILE* pipe = popen(pmapCmd.c_str(), "r");
        if (!pipe)
            return false;

        char buffer[2048];
        std::vector<std::string> lines;

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            lines.push_back(std::string(buffer));
        }

        int status = pclose(pipe);
        if (status != 0)
            return false;

        bool foundHeader = false;
        for (const auto& line : lines) {
            if (!foundHeader) {
                if (line.find("Address") != std::string::npos &&
                    line.find("Perm") != std::string::npos) {
                    foundHeader = true;
                }
                continue;
            }

            if (line.find("========") != std::string::npos ||
                line.find("KB") != std::string::npos) {
                continue;
            }

            if (line.length() > 10) {
                VmaRegion region = ParsePmapLine(line);
                if (region.startAddr != 0) {
                    regions_.push_back(region);
                }
            }
        }

        return !regions_.empty();
    }

    VmaRegion ParsePmapLine(const std::string& line) {
        // Regex pattern for pmap -XX output line:
        // Address Perm Offset Device Inode Size ... (many numeric fields) ... VmFlags [Mapping]
        static const std::regex pmapPattern(
            R"(\s*([0-9a-fA-F]+)\s+)"  // Address (hex)
            R"(([rwx-]+p?)\s+)"        // Permissions
            R"(([0-9a-fA-F]+)\s+)"     // Offset (hex)
            R"(([0-9a-fA-F:]+)\s+)"    // Device
            R"((\d+)\s+)"              // Inode
            R"((\d+)\s+)"              // Size (kB)
            R"((?:\d+\s+){16,20})"     // Skip 16-20 numeric fields (flexible)
            R"(([a-z\s]+?)\s*)"        // VmFlags (letters and spaces)
            R"(([^\s].*?)?)"           // Optional mapping name
            R"(\s*$)"                  // End of line
        );

        std::smatch match;
        if (!std::regex_match(line, match, pmapPattern)) {
            // Try simpler pattern if full pattern fails - just get the basics
            static const std::regex simplePattern(
                R"(\s*([0-9a-fA-F]+)\s+([rwx-]+p?)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F:]+)\s+(\d+)\s+(\d+).*)");

            if (!std::regex_match(line, match, simplePattern)) {
                cout << "Failed to parse pmap line: " << line;
                return VmaRegion(0, 0, "", "", false);
            }

            // For simple pattern, we don't have mapping info
            ADDRINT startAddr = std::stoull(match[1].str(), nullptr, 16);
            std::string permissions = match[2].str();
            UINT64 size = std::stoull(match[6].str()) * 1024;
            ADDRINT endAddr = startAddr + size;

            // Try to extract mapping from the end of the original line
            std::string mapping = "[anonymous]";
            std::string remaining = line.substr(match[0].length());

            // Look for non-VmFlag text at the end
            std::istringstream iss(remaining);
            std::string token;
            std::vector<std::string> tokens;
            while (iss >> token) {
                tokens.push_back(token);
            }

            // Find the last token that's not a VmFlag
            for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
                if (it->length() > 2 && *it != "rd" && *it != "wr" &&
                    *it != "ex" && *it != "mr" && *it != "mw" && *it != "me" &&
                    *it != "dw" && *it != "ac" && *it != "sd" && *it != "gd" &&
                    *it != "pf" && *it != "io" && *it != "de" && *it != "dd") {
                    mapping = *it;
                    break;
                }
            }

            bool isPrivate = IsPrivateRegion(permissions, mapping);
            cout << "Parsed (simple): [" << std::hex << startAddr << ", "
                 << endAddr << ") " << std::dec << "Perms: " << permissions
                 << ", Mapping: " << mapping
                 << ", Private: " << (isPrivate ? "Yes" : "No") << "\n";

            return VmaRegion(startAddr, endAddr, permissions, mapping,
                             isPrivate);
        }

        // Extract matched groups from full pattern
        ADDRINT startAddr = std::stoull(match[1].str(), nullptr, 16);
        std::string permissions = match[2].str();
        UINT64 size =
            std::stoull(match[6].str()) * 1024;  // Convert kB to bytes
        ADDRINT endAddr = startAddr + size;

        // Extract mapping name
        std::string mapping = "[anonymous]";  // Default

        if (match.size() > 8 && match[8].matched && !match[8].str().empty()) {
            mapping = match[8].str();
            // Clean up whitespace
            while (!mapping.empty() && std::isspace(mapping.back())) {
                mapping.pop_back();
            }
            while (!mapping.empty() && std::isspace(mapping.front())) {
                mapping.erase(0, 1);
            }
        }

        // Determine if this is a private region
        bool isPrivate = IsPrivateRegion(permissions, mapping);

        // cout << "Parsed (full): [" << std::hex << startAddr << ", " << endAddr
        //      << ") " << std::dec << "Perms: " << permissions
        //      << ", Mapping: " << mapping
        //      << ", Private: " << (isPrivate ? "Yes" : "No") << "\n";

        return VmaRegion(startAddr, endAddr, permissions, mapping, isPrivate);
    }

    bool IsPrivateRegion(const std::string& permissions,
                         const std::string& mapping) {
        // A region is considered "private" if:
        // 1. Has 'p' flag (private mapping, not shared)
        // 2. Is writable ('w' flag) - typically heap/stack/data
        // 3. Is anonymous or a specific private region type

        bool hasPrivateFlag = permissions.find('p') != std::string::npos;
        bool isWritable = permissions.find('w') != std::string::npos;

        // Identify anonymous/private regions more accurately
        bool isAnonymousOrPrivate = false;

        if (mapping == "[anonymous]" || mapping == "[heap]" ||
            mapping == "[stack]") {
            isAnonymousOrPrivate = true;
        } else if (mapping.find("[") == 0 &&
                   mapping.find("]") == mapping.length() - 1) {
            // Special regions like [vvar], [vdso], [vsyscall] - not private
            isAnonymousOrPrivate = false;
        } else if (mapping.find(".so") != std::string::npos ||
                   mapping.find("lib") == 0 ||
                   mapping.find("/") != std::string::npos) {
            // Shared libraries or absolute paths - not private
            isAnonymousOrPrivate = false;
        } else if (mapping == "[anonymous]" || mapping.length() == 0) {
            // True anonymous regions
            isAnonymousOrPrivate = true;
        } else {
            // Executable names (like "BTree") with writable sections could be private data
            // But only if they're writable (like .data, .bss sections)
            isAnonymousOrPrivate = isWritable;
        }

        return hasPrivateFlag && isWritable && isAnonymousOrPrivate;
    }
};

// --- Simulator Class ---
class Simulator {
   public:
    Simulator(const SimConfig& config,
              std::unique_ptr<std::ofstream> outStream = nullptr)
        : config_(config),
          physicalMemory_(config.PhysicalMemBytes()),
          cacheHierarchy_(
              config.cache.l1Size, config.cache.l1Ways, config.cache.l1Line,
              config.cache.l2Size, config.cache.l2Ways, config.cache.l2Line,
              config.cache.l3Size, config.cache.l3Ways, config.cache.l3Line),
          pageTable_(physicalMemory_, cacheHierarchy_, config.pgtbl.pteCachable,
                     config.tlb.l1Size, config.tlb.l1Ways, config.tlb.l2Size,
                     config.tlb.l2Ways, config.pwc.pgdSize, config.pwc.pgdWays,
                     config.pwc.pudSize, config.pwc.pudWays, config.pwc.pmdSize,
                     config.pwc.pmdWays, config.pgtbl.pgdSize,
                     config.pgtbl.pudSize, config.pgtbl.pmdSize,
                     config.pgtbl.pteSize, config.pgtbl.tocEnabled,
                     config.pgtbl.tocSize),
          outStream_(std::move(outStream)),
          bufferCount_(0),
          privatePageAccesses_(0),
          totalAccesses_(0) {

        // Initialize VMA cache only if private page tracking is enabled
        if (KnobTrackPrivatePages.Value()) {
            vmaCache_.UpdateFromMaps(PIN_GetPid(), 0);
        }
    }

    void ProcessBatch(const MemrefExtended* buffer, UINT64 numElements) {
        bool hasUnknownAccess = false;

        for (UINT64 i = 0; i < numElements; ++i) {
            const MemrefExtended& ref = buffer[i];
            accessCount_++;
            totalAccesses_++;

            // Track private page access statistics only if feature is enabled
            if (KnobTrackPrivatePages.Value() && ref.isPrivatePage) {
                privatePageAccesses_++;
            }

            // Check if this access falls outside known VMA regions (only if feature enabled)
            // This will trigger immediate cache updates when unknown regions are accessed
            if (KnobTrackPrivatePages.Value() &&
                vmaCache_.IsAddressUnknown(ref.ea)) {
                hasUnknownAccess = true;
            }

            const ADDRINT vaddr = ref.ea;
            const ADDRINT paddr = pageTable_.Translate(vaddr);
            UINT64 value = 0;
            cacheHierarchy_.Access(paddr, value, !ref.read);

            if (accessCount_ % 10000000 == 0) {
                if (KnobInstrThreshold.Value()) {
                    cout << "Instruction count: " << gInstrCount / 1000000
                         << "M, ";
                }
                cout << "Processed " << (accessCount_ / 10000000)
                     << "*10M accesses";

                if (KnobTrackPrivatePages.Value()) {
                    cout << " (Private: "
                         << (privatePageAccesses_ * 100.0 / totalAccesses_)
                         << "%)";
                }

                cout << "\r" << std::flush;
            }
        }

        // Update VMA cache when unknown accesses are detected (cache-miss triggered)
        if (KnobTrackPrivatePages.Value() &&
            vmaCache_.NeedsUpdate(bufferCount_, hasUnknownAccess)) {
            vmaCache_.UpdateFromMaps(PIN_GetPid(), bufferCount_);
        }

        bufferCount_++;

        if (KnobInstrThreshold.Value() &&
            gInstrCount >= KnobInstrThreshold.Value()) {
            this->PrintStats();
            std::flush(*outStream_);
            PIN_ExitProcess(0);
        }
    }

    void PrintStats() {
        pageTable_.PrintDetailedStats(*outStream_);
        pageTable_.PrintMemoryStats(*outStream_);
        cacheHierarchy_.PrintStats(*outStream_);

        // Print private page access statistics only if feature is enabled
        if (KnobTrackPrivatePages.Value()) {
            *outStream_ << "\n=== PRIVATE PAGE ACCESS ANALYSIS ===\n";
            *outStream_ << "Total memory accesses: " << totalAccesses_ << "\n";
            *outStream_ << "Private page accesses: " << privatePageAccesses_
                        << "\n";
            *outStream_ << "Private page ratio: "
                        << (privatePageAccesses_ * 100.0 / totalAccesses_)
                        << "%\n";
            *outStream_ << "Buffer batches processed: " << bufferCount_ << "\n";

            vmaCache_.PrintStats(*outStream_);
            *outStream_ << "\n";
        } else {
            *outStream_ << "\n=== PRIVATE PAGE TRACKING ===\n";
            *outStream_ << "Private page tracking: DISABLED\n";
            *outStream_ << "Total memory accesses: " << totalAccesses_ << "\n";
            *outStream_ << "Buffer batches processed: " << bufferCount_
                        << "\n\n";
        }
    }

    VmaCache vmaCache_;  // Make public for access in instrumentation

   private:
    SimConfig config_;
    PhysicalMemory physicalMemory_;
    CacheHierarchy cacheHierarchy_;
    PageTable pageTable_;
    UINT64 accessCount_ = 0;
    UINT64 bufferCount_ = 0;
    UINT64 privatePageAccesses_ = 0;
    UINT64 totalAccesses_ = 0;
    std::unique_ptr<std::ofstream> outStream_;
};

// Global simulator pointer for access in instrumentation
Simulator* gSimulator = nullptr;

VOID DoCount() {
    gInstrCount++;
}

VOID Instruction(INS ins, VOID* v) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)DoCount, IARG_END);
}

// --- Analysis function to set private page flag ---
VOID SetPrivatePageFlag(ADDRINT addr, VOID* bufPtr) {
    if (!KnobTrackPrivatePages.Value() || !gSimulator)
        return;

    MemrefExtended* memref = static_cast<MemrefExtended*>(bufPtr);
    memref->isPrivatePage = gSimulator->vmaCache_.IsPrivatePage(addr);
}

// --- Pin Instrumentation ---
VOID Trace(TRACE trace, VOID* v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            if (!INS_IsStandardMemop(ins))
                continue;

            UINT64 memOps = INS_MemoryOperandCount(ins);
            for (UINT64 memOp = 0; memOp < memOps; memOp++) {
                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    INS_InsertFillBuffer(
                        ins, IPOINT_BEFORE, bufId, IARG_INST_PTR,
                        offsetof(MemrefExtended, pc), IARG_MEMORYOP_EA, memOp,
                        offsetof(MemrefExtended, ea), IARG_UINT64,
                        INS_MemoryOperandSize(ins, memOp),
                        offsetof(MemrefExtended, size), IARG_BOOL,
                        INS_MemoryOperandIsRead(ins, memOp),
                        offsetof(MemrefExtended, read), IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertFillBuffer(
                        ins, IPOINT_BEFORE, bufId, IARG_INST_PTR,
                        offsetof(MemrefExtended, pc), IARG_MEMORYOP_EA, memOp,
                        offsetof(MemrefExtended, ea), IARG_UINT64,
                        INS_MemoryOperandSize(ins, memOp),
                        offsetof(MemrefExtended, size), IARG_BOOL,
                        INS_MemoryOperandIsRead(ins, memOp),
                        offsetof(MemrefExtended, read), IARG_END);
                }
            }
        }
    }
}

// --- Buffer Handling ---
VOID* BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT* ctx, VOID* buf,
                 UINT64 numElements, VOID* v) {
    Simulator* simulator = static_cast<Simulator*>(v);
    MemrefExtended* buffer = static_cast<MemrefExtended*>(buf);

    // Set private page flags for all memory references in this batch (only if feature enabled)
    if (KnobTrackPrivatePages.Value()) {
        for (UINT64 i = 0; i < numElements; ++i) {
            buffer[i].isPrivatePage =
                simulator->vmaCache_.IsPrivatePage(buffer[i].ea);
        }
    } else {
        // When feature is disabled, set all flags to false
        for (UINT64 i = 0; i < numElements; ++i) {
            buffer[i].isPrivatePage = false;
        }
    }

    simulator->ProcessBatch(buffer, numElements);
    return buf;
}

// --- Fini Function ---
VOID Fini(INT32 code, VOID* v) {
    Simulator* simulator = static_cast<Simulator*>(v);
    simulator->PrintStats();
    delete simulator;
}

// --- Usage ---
INT32 Usage() {
    cerr << "This Pin tool instruments a program and simulates its memory "
            "hierarchy.\n";
    cerr << "Use -track_private 1 to enable private page tracking (requires "
            "pmap -XX).\n";
    cerr << "VMA cache updates are triggered by cache misses for optimal "
            "performance.\n";
    cerr << KNOB_BASE::StringKnobSummary() << '\n';
    return -1;
}

// --- Main ---
int main(int argc, char* argv[]) {
    if (PIN_Init(argc, argv))
        return Usage();

    // Configure simulator using knobs
    SimConfig config;
    config.physMemGb = KnobPhysMemGB.Value();
    config.tlb.l1Size = KnobL1TLBSize.Value();
    config.tlb.l1Ways = KnobL1TLBWays.Value();
    config.tlb.l2Size = KnobL2TLBSize.Value();
    config.tlb.l2Ways = KnobL2TLBWays.Value();
    config.pwc.pgdSize = KnobPGDPWCSize.Value();
    config.pwc.pgdWays = KnobPGDPWCWays.Value();
    config.pwc.pudSize = KnobPUDPWCSize.Value();
    config.pwc.pudWays = KnobPUDPWCWays.Value();
    config.pwc.pmdSize = KnobPMDPWCSize.Value();
    config.pwc.pmdWays = KnobPMDPWCWays.Value();
    config.cache.l1Size = KnobL1CacheSize.Value();
    config.cache.l1Ways = KnobL1Ways.Value();
    config.cache.l1Line = KnobL1Line.Value();
    config.cache.l2Size = KnobL2CacheSize.Value();
    config.cache.l2Ways = KnobL2Ways.Value();
    config.cache.l2Line = KnobL2Line.Value();
    config.cache.l3Size = KnobL3CacheSize.Value();
    config.cache.l3Ways = KnobL3Ways.Value();
    config.cache.l3Line = KnobL3Line.Value();
    config.pgtbl.pteCachable = (KnobPteCachable.Value() != 0);
    config.pgtbl.pgdSize = KnobPGDSize.Value();
    config.pgtbl.pudSize = KnobPUDSize.Value();
    config.pgtbl.pmdSize = KnobPMDSize.Value();
    config.pgtbl.pteSize = KnobPTESize.Value();
    config.pgtbl.tocEnabled = KnobTOCEnabled.Value();
    config.pgtbl.tocSize = KnobTOCSize.Value();

    // Open output file
    auto outFile = std::make_unique<std::ofstream>(KnobOutputFile.Value());
    if (!outFile->is_open()) {
        cerr << "Error: Unable to open output file " << KnobOutputFile.Value()
             << '\n';
        return 1;
    }

    config.Print(*outFile);

    // Initialize simulator
    Simulator* simulator = new Simulator(config, std::move(outFile));
    gSimulator = simulator;  // Store global reference for instrumentation

    // Define trace buffer with extended MemrefExtended structure
    bufId = PIN_DefineTraceBuffer(sizeof(MemrefExtended), kNumBufPages,
                                  BufferFull, simulator);
    if (bufId == BUFFER_ID_INVALID) {
        cerr << "Error: Buffer initialization failed" << '\n';
        return 1;
    }

    // Register instrumentation and fini functions
    if (KnobInstrThreshold.Value() > 0) {
        INS_AddInstrumentFunction(Instruction, 0);
    }
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, simulator);

    // Start the program
    PIN_StartProgram();
    return 0;
}