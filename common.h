#pragma once

#include <cstdint>
#include <iostream>
#include <string>

// Define types to match those in the original simulator
typedef uint64_t ADDRINT;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// Constants for page table
constexpr UINT64 MEMTRACE_PAGE_SIZE = 4096;  // 4KB pages
constexpr UINT32 PAGE_SHIFT = 12;            // log2(MEMTRACE_PAGE_SIZE)
constexpr UINT64 PAGE_MASK =
    MEMTRACE_PAGE_SIZE - 1;  // Mask for offset within page
constexpr UINT64 PHYSICAL_MEMORY_SIZE = 1ULL << 40;  // 1TB physical memory

// Memory reference structure - matches the format in the trace file
struct MEMREF {
    ADDRINT pc;   // Program counter (8 bytes)
    ADDRINT ea;   // Effective address (8 bytes)
    UINT32 size;  // Size of memory access (4 bytes)
    UINT32 read;  // Is this a read (1) or write (0) (4 bytes)
};
// Ensure the struct has no padding
static_assert(sizeof(MEMREF) == 24, "MEMREF struct has unexpected padding");

// make a constexpr version of log2
constexpr UINT32 static_log2(UINT32 n) {
    return (n == 0) ? 0 : (31 - __builtin_clz(n));
}

struct SimConfig {
    UINT64 phys_mem_gb = 30;
    struct {
        std::size_t l1_size = 64;
        std::size_t l1_ways = 4;
        std::size_t l2_size = 1024;
        std::size_t l2_ways = 8;
    } tlb;
    struct {
        std::size_t pgdSize = 4;
        std::size_t pgdWays = 4;
        std::size_t pudSize = 4;
        std::size_t pudWays = 4;
        std::size_t pmdSize = 16;
        std::size_t pmdWays = 4;
    } pwc;
    struct {
        std::size_t l1_size = 32 * 1024;  // 32KB
        std::size_t l1_ways = 8;
        std::size_t l1_line = 64;
        std::size_t l2_size = 256 * 1024;  // 256KB
        std::size_t l2_ways = 16;
        std::size_t l2_line = 64;
        std::size_t l3_size = 8 * 1024 * 1024;  // 8MB
        std::size_t l3_ways = 16;
        std::size_t l3_line = 64;
    } cache;

    struct {
        std::size_t pgd_size = 512;
        std::size_t pud_size = 512;
        std::size_t pmd_size = 512;
        std::size_t pte_size = 512;
        bool pte_cachable = true;
        bool TOCEnabled = false;
        UINT32 TOCSize = 0;  // Size of the table of contents (TOC) in bytes
    } pgtbl;

    std::string trace_file;  // Path to the trace file
    std::size_t batch_size =
        4096;  // Number of MEMREF entries to process in each batch

    UINT64 physical_mem_bytes() const { return phys_mem_gb * (1ULL << 30); }

    void print(std::ostream& os = std::cout) const {
        os << "Simulation Configuration:\n"
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
           << "L3 Cache:           " << cache.l3_size / (1024 * 1024) << "MB, "
           << cache.l3_ways << "-way, " << cache.l3_line << "B line\n"
           << "PTE Cacheable:      " << (pgtbl.pte_cachable ? "true" : "false")
           << "\n"
           << "PGD Size:           " << pgtbl.pgd_size << " entries\n"
           << "PUD Size:           " << pgtbl.pud_size << " entries\n"
           << "PMD Size:           " << pgtbl.pmd_size << " entries\n"
           << "PTE Size:           " << pgtbl.pte_size << " entries\n"
           << "TOC Enabled:        " << (pgtbl.TOCEnabled ? "true" : "false")
           << "\n"
           << "TOC Size:          " << pgtbl.TOCSize << "\n";
    }
};
