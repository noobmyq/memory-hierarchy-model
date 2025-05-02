// offline_analyzer.cpp
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "common.h"
#include "data_cache.h"
#include "page_table.h"

using std::cerr;
using std::cout;
using std::endl;

// --- Offline Analyzer Class ---
class OfflineAnalyzer {
   public:
    OfflineAnalyzer(const SimConfig& config)
        : config_(config),
          physical_memory_(config.physical_mem_bytes()),
          cache_hierarchy_(
              config.cache.l1_size, config.cache.l1_ways, config.cache.l1_line,
              config.cache.l2_size, config.cache.l2_ways, config.cache.l2_line,
              config.cache.l3_size, config.cache.l3_ways, config.cache.l3_line),
          page_table_(
              physical_memory_, cache_hierarchy_, config.pgtbl.pte_cachable,
              config.tlb.l1_size, config.tlb.l1_ways, config.tlb.l2_size,
              config.tlb.l2_ways, config.pwc.pgdSize, config.pwc.pgdWays,
              config.pwc.pudSize, config.pwc.pudWays, config.pwc.pmdSize,
              config.pwc.pmdWays, config.pgtbl.pgd_size, config.pgtbl.pud_size,
              config.pgtbl.pmd_size, config.pgtbl.pte_size,
              config.pgtbl.TOCEnabled, config.pgtbl.TOCSize) {}

    bool run() {
        // Open trace file
        std::ifstream input(config_.trace_file, std::ios::binary);
        if (!input.is_open()) {
            cerr << "Error: Could not open trace file: " << config_.trace_file
                 << endl;
            return false;
        }

        cout << "Starting offline analysis..." << endl;

        // Initialize buffer for batch processing
        std::vector<MEMREF> buffer(config_.batch_size);

        // Timer for progress reporting
        auto start_time = std::chrono::high_resolution_clock::now();
        auto last_report_time = start_time;

        while (true) {
            // Read a batch of MEMREF entries from the trace file
            input.read(reinterpret_cast<char*>(buffer.data()),
                       config_.batch_size * sizeof(MEMREF));

            std::streamsize bytes_read = input.gcount();
            if (bytes_read == 0) {
                // End of file reached
                break;
            }

            // Calculate number of complete records read
            UINT64 records_read = bytes_read / sizeof(MEMREF);
            if (bytes_read % sizeof(MEMREF) != 0) {
                cerr << "Warning: Partial record detected at end of file. "
                        "Skipping."
                     << endl;
            }

            // Process each MEMREF in the batch
            process_batch(buffer.data(), records_read);

            // Report progress every few seconds
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               current_time - last_report_time)
                               .count();

            if (elapsed >= 5) {  // Report every 5 seconds
                cout << "Processed " << access_count_ << " accesses\r"
                     << std::flush;
                last_report_time = current_time;
            }
        }

        input.close();

        // Final time calculation
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(
                                  end_time - start_time)
                                  .count();

        cout << "\nAnalysis complete in " << total_duration << " seconds."
             << endl;
        return true;
    }

    void process_batch(const MEMREF* buffer, UINT64 num_elements) {
        for (UINT64 i = 0; i < num_elements; ++i) {
            const MEMREF& ref = buffer[i];
            access_count_++;

            const ADDRINT vaddr = ref.ea;
            const ADDRINT paddr = page_table_.translate(vaddr);

            UINT64 value = 0;
            cache_hierarchy_.access(paddr, value, !ref.read);

            // Track unique virtual and physical pages
            UINT64 vpn = vaddr / MEMTRACE_PAGE_SIZE;
            UINT64 ppn = paddr / MEMTRACE_PAGE_SIZE;
            virtual_pages_[vpn]++;
            physical_pages_[ppn]++;
        }
    }

    void print_stats() {
        cout << "\n\nOffline Analysis Results:\n"
             << "========================\n"
             << "Total accesses:       " << access_count_ << "\n"
             << "Unique virtual pages: " << virtual_pages_.size() << "\n"
             << "Unique physical pages:" << physical_pages_.size() << "\n"
             << "Physical memory used: "
             << (physical_pages_.size() * MEMTRACE_PAGE_SIZE) / (1024.0 * 1024)
             << " MB\n";

        page_table_.printDetailedStats(cout);
        page_table_.printMemoryStats(cout);
        cache_hierarchy_.printStats(cout);

        // Optionally save detailed output to a file
        std::string output_file = config_.trace_file + ".analysis.txt";
        std::ofstream outfile(output_file);
        if (outfile.is_open()) {
            outfile << "Offline Analysis Results:\n"
                    << "========================\n"
                    << "Total accesses:       " << access_count_ << "\n"
                    << "Unique virtual pages: " << virtual_pages_.size() << "\n"
                    << "Unique physical pages:" << physical_pages_.size()
                    << "\n"
                    << "Physical memory used: "
                    << (physical_pages_.size() * MEMTRACE_PAGE_SIZE) /
                           (1024.0 * 1024)
                    << " MB\n";

            page_table_.printDetailedStats(outfile);
            page_table_.printMemoryStats(outfile);
            cache_hierarchy_.printStats(outfile);

            outfile.close();
            cout << "Detailed results saved to " << output_file << endl;
        }
    }

   private:
    SimConfig config_;
    PhysicalMemory physical_memory_;
    CacheHierarchy cache_hierarchy_;
    PageTable page_table_;
    UINT64 access_count_ = 0;
    std::unordered_map<UINT64, UINT64> virtual_pages_;
    std::unordered_map<UINT64, UINT64> physical_pages_;
};

// --- Command Line Argument Parsing ---
SimConfig parse_args(int argc, char* argv[]) {
    SimConfig config;

    // Default values are already set in the SimConfig struct

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            cout << "Usage: " << argv[0] << " [options] <trace_file>\n"
                 << "Options:\n"
                 << "  -h, --help                Show this help message\n"
                 << "  --phys_mem_gb N           Physical memory size in GB "
                    "(default: 1)\n"
                 << "  --batch_size N            Batch size for processing "
                    "(default: 4096)\n"
                 << "  --l1_tlb_size N           L1 TLB size (default: 64)\n"
                 << "  --l1_tlb_ways N           L1 TLB associativity "
                    "(default: 4)\n"
                 << "  --l2_tlb_size N           L2 TLB size (default: 1024)\n"
                 << "  --l2_tlb_ways N           L2 TLB associativity "
                    "(default: 8)\n"
                 << "  --l1_cache_size N         L1 Cache size in bytes "
                    "(default: 32768)\n"
                 << "  --l1_ways N               L1 Cache associativity "
                    "(default: 8)\n"
                 << "  --l1_line N               L1 Cache line size (default: "
                    "64)\n"
                 << "  --l2_cache_size N         L2 Cache size in bytes "
                    "(default: 262144)\n"
                 << "  --l2_ways N               L2 Cache associativity "
                    "(default: 16)\n"
                 << "  --l2_line N               L2 Cache line size (default: "
                    "64)\n"
                 << "  --l3_cache_size N         L3 Cache size in bytes "
                    "(default: 8388608)\n"
                 << "  --l3_ways N               L3 Cache associativity "
                    "(default: 16)\n"
                 << "  --l3_line N               L3 Cache line size (default: "
                    "64)\n"
                 << "  --pte_cachable BOOL       PTE cacheable flag (default: "
                    "0)\n"
                 << "  --pgd_size N              PGD size in entries "
                    "(default: 512)\n"
                 << "  --pud_size N              PUD size in entries "
                    "(default: 512)\n"
                 << "  --pmd_size N              PMD size in entries "
                    "(default: 512)\n"
                 << "  --pte_size N              PTE size in entries "
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
                 << "  <trace_file>              Path to the trace file\n"
                 << endl;
            exit(0);
        } else if (arg == "--phys_mem_gb" && i + 1 < argc) {
            config.phys_mem_gb = std::stoull(argv[++i]);
        } else if (arg == "--batch_size" && i + 1 < argc) {
            config.batch_size = std::stoull(argv[++i]);
        } else if (arg == "--l1_tlb_size" && i + 1 < argc) {
            config.tlb.l1_size = std::stoull(argv[++i]);
        } else if (arg == "--l1_tlb_ways" && i + 1 < argc) {
            config.tlb.l1_ways = std::stoull(argv[++i]);
        } else if (arg == "--l2_tlb_size" && i + 1 < argc) {
            config.tlb.l2_size = std::stoull(argv[++i]);
        } else if (arg == "--l2_tlb_ways" && i + 1 < argc) {
            config.tlb.l2_ways = std::stoull(argv[++i]);
        } else if (arg == "--l1_cache_size" && i + 1 < argc) {
            config.cache.l1_size = std::stoull(argv[++i]);
        } else if (arg == "--l1_ways" && i + 1 < argc) {
            config.cache.l1_ways = std::stoull(argv[++i]);
        } else if (arg == "--l1_line" && i + 1 < argc) {
            config.cache.l1_line = std::stoull(argv[++i]);
        } else if (arg == "--l2_cache_size" && i + 1 < argc) {
            config.cache.l2_size = std::stoull(argv[++i]);
        } else if (arg == "--l2_ways" && i + 1 < argc) {
            config.cache.l2_ways = std::stoull(argv[++i]);
        } else if (arg == "--l2_line" && i + 1 < argc) {
            config.cache.l2_line = std::stoull(argv[++i]);
        } else if (arg == "--l3_cache_size" && i + 1 < argc) {
            config.cache.l3_size = std::stoull(argv[++i]);
        } else if (arg == "--l3_ways" && i + 1 < argc) {
            config.cache.l3_ways = std::stoull(argv[++i]);
        } else if (arg == "--l3_line" && i + 1 < argc) {
            config.cache.l3_line = std::stoull(argv[++i]);
        } else if (arg == "--pte_cachable" && i + 1 < argc) {
            config.pgtbl.pte_cachable = (std::stoi(argv[++i]) != 0);
        } else if (arg == "--pgd_size" && i + 1 < argc) {
            config.pgtbl.pgd_size = std::stoull(argv[++i]);
        } else if (arg == "--pud_size" && i + 1 < argc) {
            config.pgtbl.pud_size = std::stoull(argv[++i]);
        } else if (arg == "--pmd_size" && i + 1 < argc) {
            config.pgtbl.pmd_size = std::stoull(argv[++i]);
        } else if (arg == "--pte_size" && i + 1 < argc) {
            config.pgtbl.pte_size = std::stoull(argv[++i]);
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
            config.pgtbl.TOCEnabled = (std::stoi(argv[++i]) != 0);
        } else if (arg == "--toc_size" && i + 1 < argc) {
            config.pgtbl.TOCSize = std::stoull(argv[++i]);
        } else if (config.trace_file.empty() && arg[0] != '-') {
            // Assume this is the trace file
            config.trace_file = arg;
        } else {
            cerr << "Unknown option: " << arg << endl;
            exit(1);
        }
    }

    if (config.trace_file.empty()) {
        cerr << "Error: No trace file specified" << endl;
        exit(1);
    }

    return config;
}

// --- Main Function ---
int main(int argc, char* argv[]) {
    cout << "Memory Hierarchy Offline Analyzer" << endl;
    cout << "=================================" << endl;

    // Parse command line arguments
    SimConfig config = parse_args(argc, argv);

    // Print configuration
    config.print();

    // Create and run the offline analyzer
    OfflineAnalyzer analyzer(config);
    if (!analyzer.run()) {
        cerr << "Error during analysis" << endl;
        return 1;
    }

    // Print final statistics
    analyzer.print_stats();

    return 0;
}