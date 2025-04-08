#include <iostream>
#include <unordered_map>
#include "common.h"
#include "data_cache.h"
#include "page_table.h"
#include "pin.H"

using std::cerr;
using std::cout;
using std::endl;

static_assert(sizeof(MEMREF) == 24, "MEMREF struct has unexpected padding");

// --- Simulator Configuration ---
struct SimConfig {
    UINT64 phys_mem_gb = 1;
    struct {
        size_t l1_size = 64;
        size_t l1_ways = 4;
        size_t l2_size = 1024;
        size_t l2_ways = 8;
    } tlb;
    struct {
        size_t pgdSize = 16;
        size_t pgdWays = 4;
        size_t pudSize = 16;
        size_t pudWays = 4;
        size_t pmdSize = 16;
        size_t pmdWays = 4;
    } pwc;
    struct {
        size_t l1_size = 32 * 1024;  // 32KB
        size_t l1_ways = 8;
        size_t l1_line = 64;
        size_t l2_size = 256 * 1024;  // 256KB
        size_t l2_ways = 16;
        size_t l2_line = 64;
        size_t l3_size = 8 * 1024 * 1024;  // 8MB
        size_t l3_ways = 16;
        size_t l3_line = 64;
    } cache;

    bool pte_cachable = true;
    UINT64 physical_mem_bytes() const { return phys_mem_gb * (1ULL << 30); }

    void print() const {
        cout << "\nSimulation Configuration:\n"
             << "========================\n"
             << "Physical Memory:     " << phys_mem_gb << " GB\n"
             << "L1 TLB:             " << tlb.l1_size << " entries, "
             << tlb.l1_ways << "-way\n"
             << "L2 TLB:             " << tlb.l2_size << " entries, "
             << tlb.l2_ways << "-way\n"
             << "Page Walk Cache (PGD): " << pwc.pgdSize << " entries, "
             << pwc.pgdWays << "-way\n"
             << "Page Walk Cache (PUD): " << pwc.pudSize << " entries, "
             << pwc.pudWays << "-way\n"
             << "Page Walk Cache (PMD): " << pwc.pmdSize << " entries, "
             << pwc.pmdWays << "-way\n"
             << "L1 Cache:           " << cache.l1_size / 1024 << "KB, "
             << cache.l1_ways << "-way, " << cache.l1_line << "B line\n"
             << "L2 Cache:           " << cache.l2_size / 1024 << "KB, "
             << cache.l2_ways << "-way, " << cache.l2_line << "B line\n"
             << "L3 Cache:           " << cache.l3_size / (1024 * 1024)
             << "MB, " << cache.l3_ways << "-way, " << cache.l3_line
             << "B line\n"
             << "PTE Cacheable:      " << (pte_cachable ? "true" : "false")
             << endl;
    }
};

// --- Simulator Class ---
class Simulator {
   public:
    Simulator(const SimConfig& config)
        : config_(config),
          physical_memory_(config.physical_mem_bytes()),
          cache_hierarchy_(
              config.cache.l1_size, config.cache.l1_ways, config.cache.l1_line,
              config.cache.l2_size, config.cache.l2_ways, config.cache.l2_line,
              config.cache.l3_size, config.cache.l3_ways, config.cache.l3_line),
          page_table_(physical_memory_, cache_hierarchy_, config.pte_cachable,
                      config.tlb.l1_size, config.tlb.l1_ways,
                      config.tlb.l2_size, config.tlb.l2_ways,
                      config.pwc.pgdSize, config.pwc.pgdWays,
                      config.pwc.pudSize, config.pwc.pudWays,
                      config.pwc.pmdSize, config.pwc.pmdWays) {}

    void process_batch(const MEMREF* buffer, size_t numElements) {
        for (size_t i = 0; i < numElements; ++i) {
            const MEMREF& ref = buffer[i];
            access_count_++;
            const ADDRINT vaddr = ref.ea;
            const ADDRINT paddr = page_table_.translate(vaddr);
            UINT64 value = 0;
            cache_hierarchy_.access(paddr, value, !ref.read);

            UINT64 vpn = vaddr / MEMTRACE_PAGE_SIZE;
            UINT64 ppn = paddr / MEMTRACE_PAGE_SIZE;
            virtual_pages_[vpn]++;
            physical_pages_[ppn]++;

            if (access_count_ % 10000000 == 0) {
                cout << "Processed " << (access_count_ / 10000000)
                     << "*10M accesses\r" << std::flush;
            }
        }
    }

    void print_stats() {
        cout << "\n\nSimulation Results:\n"
             << "==================\n"
             << "Total accesses:       " << access_count_ << "\n"
             << "Unique virtual pages: " << virtual_pages_.size() << "\n"
             << "Unique physical pages:" << physical_pages_.size() << "\n"
             << "Physical memory used: "
             << (physical_pages_.size() * MEMTRACE_PAGE_SIZE) / (1024.0 * 1024)
             << " MB\n";
        page_table_.printDetailedStats(cout);
        page_table_.printMemoryStats(cout);
        cache_hierarchy_.printStats(cout);
    }

   private:
    SimConfig config_;
    PhysicalMemory physical_memory_;
    CacheHierarchy cache_hierarchy_;
    PageTable page_table_;
    size_t access_count_ = 0;
    std::unordered_map<UINT64, size_t> virtual_pages_;
    std::unordered_map<UINT64, size_t> physical_pages_;
};

// --- Pin Configuration ---
#define NUM_BUF_PAGES 1024
BUFFER_ID bufId;

// --- Knobs for Simulator Configuration ---
KNOB<UINT64> KnobPhysMemGB(KNOB_MODE_WRITEONCE, "pintool", "phys_mem_gb", "1",
                           "Physical memory size in GB");
KNOB<size_t> KnobL1TLBSize(KNOB_MODE_WRITEONCE, "pintool", "l1_tlb_size", "64",
                           "L1 TLB size");
KNOB<size_t> KnobL1TLBWays(KNOB_MODE_WRITEONCE, "pintool", "l1_tlb_ways", "4",
                           "L1 TLB associativity");
KNOB<size_t> KnobL2TLBSize(KNOB_MODE_WRITEONCE, "pintool", "l2_tlb_size",
                           "1024", "L2 TLB size");
KNOB<size_t> KnobL2TLBWays(KNOB_MODE_WRITEONCE, "pintool", "l2_tlb_ways", "8",
                           "L2 TLB associativity");
KNOB<size_t> KnobPGDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pgd_pwc_size",
                            "16", "PWC size");
KNOB<size_t> KnobPGDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pgd_pwc_ways", "4",
                            "PWC associativity");
KNOB<size_t> KnobPUDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pud_pwc_size",
                            "16", "PWC size");
KNOB<size_t> KnobPUDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pud_pwc_ways", "4",
                            "PWC associativity");
KNOB<size_t> KnobPMDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pmd_pwc_size",
                            "16", "PWC size");
KNOB<size_t> KnobPMDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pmd_pwc_ways", "4",
                            "PWC associativity");
KNOB<size_t> KnobL1CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l1_cache_size",
                             "32768", "L1 Cache size in bytes");
KNOB<size_t> KnobL1Ways(KNOB_MODE_WRITEONCE, "pintool", "l1_ways", "8",
                        "L1 Cache associativity");
KNOB<size_t> KnobL1Line(KNOB_MODE_WRITEONCE, "pintool", "l1_line", "64",
                        "L1 Cache line size");
KNOB<size_t> KnobL2CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l2_cache_size",
                             "262144", "L2 Cache size in bytes");
KNOB<size_t> KnobL2Ways(KNOB_MODE_WRITEONCE, "pintool", "l2_ways", "16",
                        "L2 Cache associativity");
KNOB<size_t> KnobL2Line(KNOB_MODE_WRITEONCE, "pintool", "l2_line", "64",
                        "L2 Cache line size");
KNOB<size_t> KnobL3CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l3_cache_size",
                             "8388608", "L3 Cache size in bytes");
KNOB<size_t> KnobL3Ways(KNOB_MODE_WRITEONCE, "pintool", "l3_ways", "16",
                        "L3 Cache associativity");
KNOB<size_t> KnobL3Line(KNOB_MODE_WRITEONCE, "pintool", "l3_line", "64",
                        "L3 Cache line size");
KNOB<bool> KnobPteCachable(KNOB_MODE_WRITEONCE, "pintool", "pte_cachable", "1",
                           "PTE cacheable flag");

// --- Pin Instrumentation ---
VOID Trace(TRACE trace, VOID* v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            if (!INS_IsStandardMemop(ins))
                continue;
            UINT32 memOps = INS_MemoryOperandCount(ins);
            for (UINT32 memOp = 0; memOp < memOps; memOp++) {
                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp,
                                         offsetof(MEMREF, ea), IARG_UINT32,
                                         INS_MemoryOperandSize(ins, memOp),
                                         offsetof(MEMREF, size), IARG_BOOL,
                                         INS_MemoryOperandIsRead(ins, memOp),
                                         offsetof(MEMREF, read), IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp,
                                         offsetof(MEMREF, ea), IARG_UINT32,
                                         INS_MemoryOperandSize(ins, memOp),
                                         offsetof(MEMREF, size), IARG_BOOL,
                                         INS_MemoryOperandIsRead(ins, memOp),
                                         offsetof(MEMREF, read), IARG_END);
                }
            }
        }
    }
}

// --- Buffer Handling ---
VOID* BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT* ctx, VOID* buf,
                 UINT64 numElements, VOID* v) {
    Simulator* simulator = static_cast<Simulator*>(v);
    simulator->process_batch(static_cast<MEMREF*>(buf), numElements);
    return buf;
}

// --- Fini Function ---
VOID Fini(INT32 code, VOID* v) {
    Simulator* simulator = static_cast<Simulator*>(v);
    simulator->print_stats();
    delete simulator;
}

// --- Usage ---
INT32 Usage() {
    cerr << "This Pin tool instruments a program and simulates its memory "
            "hierarchy.\n";
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

// --- Main ---
int main(int argc, char* argv[]) {
    if (PIN_Init(argc, argv))
        return Usage();

    // Configure simulator using knobs
    SimConfig config;
    config.phys_mem_gb = KnobPhysMemGB.Value();
    config.tlb.l1_size = KnobL1TLBSize.Value();
    config.tlb.l1_ways = KnobL1TLBWays.Value();
    config.tlb.l2_size = KnobL2TLBSize.Value();
    config.tlb.l2_ways = KnobL2TLBWays.Value();
    config.pwc.pgdSize = KnobPGDPWCSize.Value();
    config.pwc.pgdWays = KnobPGDPWCWays.Value();
    config.pwc.pudSize = KnobPUDPWCSize.Value();
    config.pwc.pudWays = KnobPUDPWCWays.Value();
    config.pwc.pmdSize = KnobPMDPWCSize.Value();
    config.pwc.pmdWays = KnobPMDPWCWays.Value();
    config.cache.l1_size = KnobL1CacheSize.Value();
    config.cache.l1_ways = KnobL1Ways.Value();
    config.cache.l1_line = KnobL1Line.Value();
    config.cache.l2_size = KnobL2CacheSize.Value();
    config.cache.l2_ways = KnobL2Ways.Value();
    config.cache.l2_line = KnobL2Line.Value();
    config.cache.l3_size = KnobL3CacheSize.Value();
    config.cache.l3_ways = KnobL3Ways.Value();
    config.cache.l3_line = KnobL3Line.Value();
    config.pte_cachable = KnobPteCachable.Value();

    config.print();

    // Initialize simulator
    Simulator* simulator = new Simulator(config);

    // Define trace buffer
    bufId = PIN_DefineTraceBuffer(sizeof(MEMREF), NUM_BUF_PAGES, BufferFull,
                                  simulator);
    if (bufId == BUFFER_ID_INVALID) {
        cerr << "Error: Buffer initialization failed" << endl;
        return 1;
    }

    // Register instrumentation and fini functions
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, simulator);

    // Start the program
    PIN_StartProgram();
    return 0;
}