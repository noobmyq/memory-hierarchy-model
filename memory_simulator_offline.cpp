// offline_analyzer.cpp
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "common.h"
#include "data_cache.h"
#include "page_table.h"

using std::cerr;
using std::cout;

// --- Offline Analyzer Class ---
class OfflineAnalyzer {
   public:
    OfflineAnalyzer(const SimConfig& config)
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
                     config.pgtbl.tocSize) {}

    bool Run() {
        // Open trace file
        std::ifstream input(config_.traceFile, std::ios::binary);
        if (!input.is_open()) {
            cerr << "Error: Could not open trace file: " << config_.traceFile
                 << '\n';
            return false;
        }

        cout << "Starting offline analysis..." << '\n';

        // Initialize buffer for batch processing
        std::vector<MEMREF> buffer(config_.batchSize);

        // Timer for progress reporting
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastReportTime = startTime;

        while (true) {
            // Read a batch of MEMREF entries from the trace file
            input.read(reinterpret_cast<char*>(buffer.data()),
                       config_.batchSize * sizeof(MEMREF));

            std::streamsize bytesRead = input.gcount();
            if (bytesRead == 0) {
                // End of file reached
                break;
            }

            // Calculate number of complete records read
            UINT64 recordsRead = bytesRead / sizeof(MEMREF);
            if (bytesRead % sizeof(MEMREF) != 0) {
                cerr << "Warning: Partial record detected at end of file. "
                        "Skipping."
                     << '\n';
            }

            // Process each MEMREF in the batch
            ProcessBatch(buffer.data(), recordsRead);

            // Report progress every few seconds
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               currentTime - lastReportTime)
                               .count();

            if (elapsed >= 5) {  // Report every 5 seconds
                cout << "Processed " << accessCount_ << " accesses\r"
                     << std::flush;
                lastReportTime = currentTime;
            }
        }

        input.close();

        // Final time calculation
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::seconds>(
                                 endTime - startTime)
                                 .count();

        cout << "\nAnalysis complete in " << totalDuration << " seconds."
             << '\n';
        return true;
    }

    void ProcessBatch(const MEMREF* buffer, UINT64 numElements) {
        for (UINT64 i = 0; i < numElements; ++i) {
            const MEMREF& ref = buffer[i];
            accessCount_++;

            const ADDRINT vaddr = ref.ea;
            const ADDRINT paddr = pageTable_.Translate(vaddr);

            UINT64 value = 0;
            cacheHierarchy_.Access(paddr, value, !ref.read);

            // Track unique virtual and physical pages
            UINT64 vpn = vaddr / kMemTracePageSize;
            UINT64 ppn = paddr / kMemTracePageSize;
            virtualPages_[vpn]++;
            physicalPages_[ppn]++;
        }
    }

    void PrintStats() {
        cout << "\n\nOffline Analysis Results:\n"
             << "========================\n"
             << "Total accesses:       " << accessCount_ << "\n"
             << "Unique virtual pages: " << virtualPages_.size() << "\n"
             << "Unique physical pages:" << physicalPages_.size() << "\n"
             << "Physical memory used: "
             << (physicalPages_.size() * kMemTracePageSize) / (1024.0 * 1024)
             << " MB\n";

        pageTable_.PrintDetailedStats(cout);
        pageTable_.PrintMemoryStats(cout);
        cacheHierarchy_.PrintStats(cout);

        // Optionally save detailed output to a file
        std::string outputFile = config_.traceFile + ".analysis.txt";
        std::ofstream outfile(outputFile);
        if (outfile.is_open()) {
            outfile << "Offline Analysis Results:\n"
                    << "========================\n"
                    << "Total accesses:       " << accessCount_ << "\n"
                    << "Unique virtual pages: " << virtualPages_.size() << "\n"
                    << "Unique physical pages:" << physicalPages_.size() << "\n"
                    << "Physical memory used: "
                    << (physicalPages_.size() * kMemTracePageSize) /
                           (1024.0 * 1024)
                    << " MB\n";

            pageTable_.PrintDetailedStats(outfile);
            pageTable_.PrintMemoryStats(outfile);
            cacheHierarchy_.PrintStats(outfile);

            outfile.close();
            cout << "Detailed results saved to " << outputFile << '\n';
        }
    }

   private:
    SimConfig config_;
    PhysicalMemory physicalMemory_;
    CacheHierarchy cacheHierarchy_;
    PageTable pageTable_;
    UINT64 accessCount_ = 0;
    std::unordered_map<UINT64, UINT64> virtualPages_;
    std::unordered_map<UINT64, UINT64> physicalPages_;
};

// --- Command Line Argument Parsing ---
SimConfig ParseArgs(int argc, char* argv[]) {
    SimConfig config;

    // Default values are already set in the SimConfig struct

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            cout << "Usage: " << argv[0] << " [options] <traceFile>\n"
                 << "Options:\n"
                 << "  -h, --help                Show this help message\n"
                 << "  --phys_mem_gb N           Physical memory size in GB "
                    "(default: 1)\n"
                 << "  --batchSize N            Batch size for processing "
                    "(default: 4096)\n"
                 << "  --l1_tlb_size N           L1 TLB size (default: 64)\n"
                 << "  --l1_tlb_ways N           L1 TLB associativity "
                    "(default: 4)\n"
                 << "  --l2_tlb_size N           L2 TLB size (default: 1024)\n"
                 << "  --l2_tlb_ways N           L2 TLB associativity "
                    "(default: 8)\n"
                 << "  --l1_cache_size N         L1 Cache size in bytes "
                    "(default: 32768)\n"
                 << "  --l1Ways N               L1 Cache associativity "
                    "(default: 8)\n"
                 << "  --l1Line N               L1 Cache line size (default: "
                    "64)\n"
                 << "  --l2_cache_size N         L2 Cache size in bytes "
                    "(default: 262144)\n"
                 << "  --l2Ways N               L2 Cache associativity "
                    "(default: 16)\n"
                 << "  --l2Line N               L2 Cache line size (default: "
                    "64)\n"
                 << "  --l3_cache_size N         L3 Cache size in bytes "
                    "(default: 8388608)\n"
                 << "  --l3_ways N               L3 Cache associativity "
                    "(default: 16)\n"
                 << "  --l3Line N               L3 Cache line size (default: "
                    "64)\n"
                 << "  --pteCachable BOOL       PTE cacheable flag (default: "
                    "0)\n"
                 << "  --pgdSize N              PGD size in entries "
                    "(default: 512)\n"
                 << "  --pudSize N              PUD size in entries "
                    "(default: 512)\n"
                 << "  --pmdSize N              PMD size in entries "
                    "(default: 512)\n"
                 << "  --pteSize N              PTE size in entries "
                    "(default: 512)\n"
                 << "  --pgd_pwc_size N          PGD PWC size in entries "
                    "(default: 4)\n"
                 << "  --pgd_pwc_ways N          PGD PWC associativity "
                    "(default: 4)\n"
                 << "  --pud_pwc_size N          PUD PWC size in entries "
                    "(default: 4)\n"
                 << "  --pud_pwc_ways N          PUD PWC associativity "
                    "(default: 4)\n"
                 << "  --pmd_pwc_size N          PMD PWC size in entries "
                    "(default: 16)\n"
                 << "  --pmd_pwc_ways N          PMD PWC associativity "
                    "(default: 4)\n"
                 << " ---toc_enabled BOOL          Enable TOC (default: 0)\n"
                 << "  --toc_size N               TOC size in bytes "
                    "(default: 0)\n"
                 << "  <traceFile>              Path to the trace file\n"
                 << '\n';
            exit(0);
        } else if (arg == "--phys_mem_gb" && i + 1 < argc) {
            config.physMemGb = std::stoull(argv[++i]);
        } else if (arg == "--batch_size" && i + 1 < argc) {
            config.batchSize = std::stoull(argv[++i]);
        } else if (arg == "--l1_tlb_size" && i + 1 < argc) {
            config.tlb.l1Size = std::stoull(argv[++i]);
        } else if (arg == "--l1_tlb_ways" && i + 1 < argc) {
            config.tlb.l1Ways = std::stoull(argv[++i]);
        } else if (arg == "--l2_tlb_size" && i + 1 < argc) {
            config.tlb.l2Size = std::stoull(argv[++i]);
        } else if (arg == "--l2_tlb_ways" && i + 1 < argc) {
            config.tlb.l2Ways = std::stoull(argv[++i]);
        } else if (arg == "--l1_cache_size" && i + 1 < argc) {
            config.cache.l1Size = std::stoull(argv[++i]);
        } else if (arg == "--l1_ways" && i + 1 < argc) {
            config.cache.l1Ways = std::stoull(argv[++i]);
        } else if (arg == "--l1_line" && i + 1 < argc) {
            config.cache.l1Line = std::stoull(argv[++i]);
        } else if (arg == "--l2_cache_size" && i + 1 < argc) {
            config.cache.l2Size = std::stoull(argv[++i]);
        } else if (arg == "--l2_ways" && i + 1 < argc) {
            config.cache.l2Ways = std::stoull(argv[++i]);
        } else if (arg == "--l2_line" && i + 1 < argc) {
            config.cache.l2Line = std::stoull(argv[++i]);
        } else if (arg == "--l3_cache_size" && i + 1 < argc) {
            config.cache.l3Size = std::stoull(argv[++i]);
        } else if (arg == "--l3_ways" && i + 1 < argc) {
            config.cache.l3Ways = std::stoull(argv[++i]);
        } else if (arg == "--l3_line" && i + 1 < argc) {
            config.cache.l3Line = std::stoull(argv[++i]);
        } else if (arg == "--pte_cachable" && i + 1 < argc) {
            config.pgtbl.pteCachable = (std::stoi(argv[++i]) != 0);
        } else if (arg == "--pgd_size" && i + 1 < argc) {
            config.pgtbl.pgdSize = std::stoull(argv[++i]);
        } else if (arg == "--pud_size" && i + 1 < argc) {
            config.pgtbl.pudSize = std::stoull(argv[++i]);
        } else if (arg == "--pmd_size" && i + 1 < argc) {
            config.pgtbl.pmdSize = std::stoull(argv[++i]);
        } else if (arg == "--pte_size" && i + 1 < argc) {
            config.pgtbl.pteSize = std::stoull(argv[++i]);
        } else if (arg == "--pgd_pwc_size" && i + 1 < argc) {
            config.pwc.pgdSize = std::stoull(argv[++i]);
        } else if (arg == "--pgd_pwc_ways" && i + 1 < argc) {
            config.pwc.pgdWays = std::stoull(argv[++i]);
        } else if (arg == "--pud_pwc_size" && i + 1 < argc) {
            config.pwc.pudSize = std::stoull(argv[++i]);
        } else if (arg == "--pud_pwc_ways" && i + 1 < argc) {
            config.pwc.pudWays = std::stoull(argv[++i]);
        } else if (arg == "--pmd_pwc_size" && i + 1 < argc) {
            config.pwc.pmdSize = std::stoull(argv[++i]);
        } else if (arg == "--pmd_pwc_ways" && i + 1 < argc) {
            config.pwc.pmdWays = std::stoull(argv[++i]);
        } else if (arg == "--toc_enabled" && i + 1 < argc) {
            config.pgtbl.tocEnabled = (std::stoi(argv[++i]) != 0);
        } else if (arg == "--toc_size" && i + 1 < argc) {
            config.pgtbl.tocSize = std::stoull(argv[++i]);
        } else if (config.traceFile.empty() && arg[0] != '-') {
            // Assume this is the trace file
            config.traceFile = arg;
        } else {
            cerr << "Unknown option: " << arg << '\n';
            exit(1);
        }
    }

    if (config.traceFile.empty()) {
        cerr << "Error: No trace file specified" << '\n';
        exit(1);
    }

    return config;
}

// --- Main Function ---
int main(int argc, char* argv[]) {
    cout << "Memory Hierarchy Offline Analyzer" << '\n';
    cout << "=================================" << '\n';

    // Parse command line arguments
    SimConfig config = ParseArgs(argc, argv);

    // Print configuration
    config.Print();

    // Create and run the offline analyzer
    OfflineAnalyzer analyzer(config);
    if (!analyzer.Run()) {
        cerr << "Error during analysis" << '\n';
        return 1;
    }

    // Print final statistics
    analyzer.PrintStats();

    return 0;
}