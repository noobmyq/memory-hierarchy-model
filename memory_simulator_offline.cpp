// offline_analyzer.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iomanip>
#include "common.h"
#include "data_cache.h"
#include "page_table.h"

using std::cerr;
using std::cout;
using std::endl;

// --- Offline Analyzer Configuration ---
struct OfflineConfig {
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

    struct {
        size_t pgd_size = 512;
        size_t pud_size = 512;
        size_t pmd_size = 512;
        size_t pte_size = 512;
        bool pte_cachable = true;
    } pgtbl;
    
    std::string trace_file;  // Path to the trace file
    size_t batch_size = 4096;  // Number of MEMREF entries to process in each batch
    
    UINT64 physical_mem_bytes() const { 
        return phys_mem_gb * (1ULL << 30); 
    }

    void print() const {
        cout << "\nOffline Analysis Configuration:\n"
             << "==============================\n"
             << "Trace File:          " << trace_file << "\n"
             << "Batch Size:          " << batch_size << " entries\n"
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
             << "PTE Cacheable:      "
             << (pgtbl.pte_cachable ? "true" : "false") << "\n"
             << "PGD Size:           " << pgtbl.pgd_size << " entries\n"
             << "PUD Size:           " << pgtbl.pud_size << " entries\n"
             << "PMD Size:           " << pgtbl.pmd_size << " entries\n"
             << "PTE Size:           " << pgtbl.pte_size << " entries\n";
    }
};

// --- Offline Analyzer Class ---
class OfflineAnalyzer {
public:
    OfflineAnalyzer(const OfflineConfig& config)
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
              config.pgtbl.pmd_size, config.pgtbl.pte_size) {}

    bool run() {
        // Open trace file
        std::ifstream input(config_.trace_file, std::ios::binary);
        if (!input.is_open()) {
            cerr << "Error: Could not open trace file: " << config_.trace_file << endl;
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
            size_t records_read = bytes_read / sizeof(MEMREF);
            if (bytes_read % sizeof(MEMREF) != 0) {
                cerr << "Warning: Partial record detected at end of file. Skipping." << endl;
            }

            // Process each MEMREF in the batch
            process_batch(buffer.data(), records_read);
            
            // Report progress every few seconds
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - last_report_time).count();
            
            if (elapsed >= 5) {  // Report every 5 seconds
                cout << "Processed " << access_count_ << " accesses\r" << std::flush;
                last_report_time = current_time;
            }
        }
        
        input.close();
        
        // Final time calculation
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(
            end_time - start_time).count();
        
        cout << "\nAnalysis complete in " << total_duration << " seconds." << endl;
        return true;
    }

    void process_batch(const MEMREF* buffer, size_t num_elements) {
        for (size_t i = 0; i < num_elements; ++i) {
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
                   << "Unique physical pages:" << physical_pages_.size() << "\n"
                   << "Physical memory used: "
                   << (physical_pages_.size() * MEMTRACE_PAGE_SIZE) / (1024.0 * 1024)
                   << " MB\n";
            
            page_table_.printDetailedStats(outfile);
            page_table_.printMemoryStats(outfile);
            cache_hierarchy_.printStats(outfile);
            
            outfile.close();
            cout << "Detailed results saved to " << output_file << endl;
        }
    }

private:
    OfflineConfig config_;
    PhysicalMemory physical_memory_;
    CacheHierarchy cache_hierarchy_;
    PageTable page_table_;
    size_t access_count_ = 0;
    std::unordered_map<UINT64, size_t> virtual_pages_;
    std::unordered_map<UINT64, size_t> physical_pages_;
};

// --- Command Line Argument Parsing ---
OfflineConfig parse_args(int argc, char* argv[]) {
    OfflineConfig config;
    
    // Default values are already set in the OfflineConfig struct
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            cout << "Usage: " << argv[0] << " [options] <trace_file>\n"
                 << "Options:\n"
                 << "  -h, --help                Show this help message\n"
                 << "  --phys-mem-gb N           Physical memory size in GB (default: 1)\n"
                 << "  --batch-size N            Batch size for processing (default: 4096)\n"
                 << "  --l1-tlb-size N           L1 TLB size (default: 64)\n"
                 << "  --l1-tlb-ways N           L1 TLB associativity (default: 4)\n"
                 << "  --l2-tlb-size N           L2 TLB size (default: 1024)\n"
                 << "  --l2-tlb-ways N           L2 TLB associativity (default: 8)\n"
                 << "  --l1-cache-size N         L1 Cache size in bytes (default: 32768)\n"
                 << "  --l1-ways N               L1 Cache associativity (default: 8)\n"
                 << "  --l1-line N               L1 Cache line size (default: 64)\n"
                 << "  --l2-cache-size N         L2 Cache size in bytes (default: 262144)\n"
                 << "  --l2-ways N               L2 Cache associativity (default: 16)\n"
                 << "  --l2-line N               L2 Cache line size (default: 64)\n"
                 << "  --l3-cache-size N         L3 Cache size in bytes (default: 8388608)\n"
                 << "  --l3-ways N               L3 Cache associativity (default: 16)\n"
                 << "  --l3-line N               L3 Cache line size (default: 64)\n"
                 << "  --pte-cachable BOOL       PTE cacheable flag (default: 1)\n"
                 << endl;
            exit(0);
        }
        else if (arg == "--phys-mem-gb" && i + 1 < argc) {
            config.phys_mem_gb = std::stoull(argv[++i]);
        }
        else if (arg == "--batch-size" && i + 1 < argc) {
            config.batch_size = std::stoull(argv[++i]);
        }
        else if (arg == "--l1-tlb-size" && i + 1 < argc) {
            config.tlb.l1_size = std::stoull(argv[++i]);
        }
        else if (arg == "--l1-tlb-ways" && i + 1 < argc) {
            config.tlb.l1_ways = std::stoull(argv[++i]);
        }
        else if (arg == "--l2-tlb-size" && i + 1 < argc) {
            config.tlb.l2_size = std::stoull(argv[++i]);
        }
        else if (arg == "--l2-tlb-ways" && i + 1 < argc) {
            config.tlb.l2_ways = std::stoull(argv[++i]);
        }
        else if (arg == "--l1-cache-size" && i + 1 < argc) {
            config.cache.l1_size = std::stoull(argv[++i]);
        }
        else if (arg == "--l1-ways" && i + 1 < argc) {
            config.cache.l1_ways = std::stoull(argv[++i]);
        }
        else if (arg == "--l1-line" && i + 1 < argc) {
            config.cache.l1_line = std::stoull(argv[++i]);
        }
        else if (arg == "--l2-cache-size" && i + 1 < argc) {
            config.cache.l2_size = std::stoull(argv[++i]);
        }
        else if (arg == "--l2-ways" && i + 1 < argc) {
            config.cache.l2_ways = std::stoull(argv[++i]);
        }
        else if (arg == "--l2-line" && i + 1 < argc) {
            config.cache.l2_line = std::stoull(argv[++i]);
        }
        else if (arg == "--l3-cache-size" && i + 1 < argc) {
            config.cache.l3_size = std::stoull(argv[++i]);
        }
        else if (arg == "--l3-ways" && i + 1 < argc) {
            config.cache.l3_ways = std::stoull(argv[++i]);
        }
        else if (arg == "--l3-line" && i + 1 < argc) {
            config.cache.l3_line = std::stoull(argv[++i]);
        }
        else if (arg == "--pte-cachable" && i + 1 < argc) {
            config.pgtbl.pte_cachable = (std::stoi(argv[++i]) != 0);
        }
        else if (arg == "--pgd-size" && i + 1 < argc) {
            config.pgtbl.pgd_size = std::stoull(argv[++i]);
        }
        else if (arg == "--pud-size" && i + 1 < argc) {
            config.pgtbl.pud_size = std::stoull(argv[++i]);
        }
        else if (arg == "--pmd-size" && i + 1 < argc) {
            config.pgtbl.pmd_size = std::stoull(argv[++i]);
        }
        else if (arg == "--pte-size" && i + 1 < argc) {
            config.pgtbl.pte_size = std::stoull(argv[++i]);
        }
        else if (arg == "--pgd-pwc-size" && i + 1 < argc) {
            config.pwc.pgdSize = std::stoull(argv[++i]);
        }
        else if (arg == "--pgd-pwc-ways" && i + 1 < argc) {
            config.pwc.pgdWays = std::stoull(argv[++i]);
        }
        else if (arg == "--pud-pwc-size" && i + 1 < argc) {
            config.pwc.pudSize = std::stoull(argv[++i]);
        }
        else if (arg == "--pud-pwc-ways" && i + 1 < argc) {
            config.pwc.pudWays = std::stoull(argv[++i]);
        }
        else if (arg == "--pmd-pwc-size" && i + 1 < argc) {
            config.pwc.pmdSize = std::stoull(argv[++i]);
        }
        else if (arg == "--pmd-pwc-ways" && i + 1 < argc) {
            config.pwc.pmdWays = std::stoull(argv[++i]);
        }
        else if (config.trace_file.empty() && arg[0] != '-') {
            // Assume this is the trace file
            config.trace_file = arg;
        }
        else {
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
    OfflineConfig config = parse_args(argc, argv);
    
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