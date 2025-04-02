#include "common.h"
#include "page_table.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <iomanip>

int main(int argc, char* argv[]) {
    // Check command line arguments
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <trace_file> [l1_tlb_size] [l1_tlb_ways] [l2_tlb_size] [l2_tlb_ways] [pwc_size] [pwc_ways] [phys_mem_gb]" << std::endl;
        return 1;
    }
    
    std::string trace_file = argv[1];
    size_t l1_tlb_size = 64;    // Default L1 TLB entries
    size_t l1_tlb_ways = 4;     // Default L1 TLB associativity
    size_t l2_tlb_size = 1024;  // Default L2 TLB entries
    size_t l2_tlb_ways = 8;     // Default L2 TLB associativity
    size_t pwc_size = 16;       // Default PWC entries
    size_t pwc_ways = 4;        // Default PWC associativity
    UINT64 phys_mem_size = PHYSICAL_MEMORY_SIZE;  // Default physical memory size (1TB)
    
    // Parse L1 TLB size if provided
    if (argc > 2) {
        l1_tlb_size = std::stoul(argv[2]);
    }
    
    // Parse L1 TLB ways if provided
    if (argc > 3) {
        l1_tlb_ways = std::stoul(argv[3]);
    }
    
    // Parse L2 TLB size if provided
    if (argc > 4) {
        l2_tlb_size = std::stoul(argv[4]);
    }
    
    // Parse L2 TLB ways if provided
    if (argc > 5) {
        l2_tlb_ways = std::stoul(argv[5]);
    }
    
    // Parse PWC size if provided
    if (argc > 6) {
        pwc_size = std::stoul(argv[6]);
    }
    
    // Parse PWC ways if provided
    if (argc > 7) {
        pwc_ways = std::stoul(argv[7]);
    }
    
    // Parse physical memory size if provided (in GB)
    if (argc > 8) {
        UINT64 phys_mem_gb = std::stoull(argv[8]);
        phys_mem_size = phys_mem_gb * (1ULL << 30);  // Convert GB to bytes
    }
    
    std::cout << "Reading trace file: " << trace_file << std::endl;
    std::cout << "L1 TLB configuration: " << l1_tlb_size << " entries, " << l1_tlb_ways << "-way set associative" << std::endl;
    std::cout << "L2 TLB configuration: " << l2_tlb_size << " entries, " << l2_tlb_ways << "-way set associative" << std::endl;
    std::cout << "PWC configuration: " << pwc_size << " entries, " << pwc_ways << "-way set associative" << std::endl;
    std::cout << "Physical memory size: " << (phys_mem_size / (1ULL << 30)) << " GB" << std::endl;
    
    // Open the trace file in binary mode
    std::ifstream input(trace_file, std::ios::binary);
    if (!input) {
        std::cerr << "Error: Could not open trace file: " << trace_file << std::endl;
        return 1;
    }
    
    // Initialize components
    PhysicalMemory physicalMemory(phys_mem_size);
    PageTable pageTable(physicalMemory, 
                        l1_tlb_size, l1_tlb_ways, 
                        l2_tlb_size, l2_tlb_ways, 
                        pwc_size, pwc_ways);
    
    // Track unique pages accessed
    std::unordered_map<UINT64, size_t> virtual_pages;
    std::unordered_map<UINT64, size_t> physical_pages;
    
    // Counter for progress reporting
    size_t access_count = 0;
    
    // Read MEMREF structures from the file
    MEMREF ref;
    while (input.read(reinterpret_cast<char*>(&ref), sizeof(MEMREF))) {
        if (input.gcount() == sizeof(MEMREF)) {
            access_count++;
            
            // Translate virtual address to physical
            ADDRINT vaddr = ref.ea;
            ADDRINT paddr = pageTable.translate(vaddr);
            
            // Track page usage
            UINT64 virtual_page = vaddr / PAGE_SIZE;
            UINT64 physical_page = paddr / PAGE_SIZE;
            virtual_pages[virtual_page]++;
            physical_pages[physical_page]++;
            
            // Progress indicator for large traces
            if (access_count % 1000000 == 0) {
                std::cout << "Processed " << access_count / 1000000 << "M memory references\r" << std::flush;
            }
        }
    }
    
    std::cout << "\nFinished processing trace file (" << access_count << " accesses)" << std::endl;
    
    // Print memory and page table statistics
    std::cout << "\nMemory Translation Summary:" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << "L1 TLB hit rate: " << pageTable.getL1TlbHitRate() * 100.0 << "%" << std::endl;
    std::cout << "L2 TLB hit rate: " << pageTable.getL2TlbHitRate() * 100.0 << "%" << std::endl;
    std::cout << "Overall TLB efficiency: " << pageTable.getTlbEfficiency() * 100.0 << "%" << std::endl;
    std::cout << "PMD PWC hit rate: " << pageTable.getPmdCacheHitRate() * 100.0 << "%" << std::endl;
    std::cout << "PUD PWC hit rate: " << pageTable.getPudCacheHitRate() * 100.0 << "%" << std::endl;
    std::cout << "PGD PWC hit rate: " << pageTable.getPgdCacheHitRate() * 100.0 << "%" << std::endl;
    std::cout << "Full page table walks: " << pageTable.getFullWalks() << std::endl;
    std::cout << "Unique virtual pages: " << virtual_pages.size() << std::endl;
    std::cout << "Unique physical pages: " << physical_pages.size() << std::endl;
    std::cout << "Physical memory usage: " 
              << (physical_pages.size() * PAGE_SIZE) / (1024.0 * 1024.0) << " MB" << std::endl;
    
    // Print detailed statistics
    pageTable.printDetailedStats(std::cout);
    
    return 0;
}