#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

// Define types to match those in the original simulator
typedef uint64_t ADDRINT;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// Constants for page table
constexpr UINT64 kMemTracePageSize = 4096;  // 4KB pages
constexpr UINT64 kPageShift = 12;           // log2(kMemTracePageSize)
constexpr UINT64 kPageMask =
    kMemTracePageSize - 1;  // Mask for offset within page
constexpr UINT64 kPhysicalMemorySize = 1ULL << 40;  // 1TB physical memory

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
constexpr UINT64 StaticLog2(UINT64 n) {
    return (n == 0) ? 0 : (31 - __builtin_clz(n));
}

struct SimConfig {
    UINT64 physMemGb = 30;
    struct {
        UINT64 l1Size = 64;
        UINT64 l1Ways = 4;
        UINT64 l2Size = 1024;
        UINT64 l2Ways = 8;
    } tlb;
    struct {
        UINT64 pgdSize = 4;
        UINT64 pgdWays = 4;
        UINT64 pudSize = 4;
        UINT64 pudWays = 4;
        UINT64 pmdSize = 16;
        UINT64 pmdWays = 4;
    } pwc;
    struct {
        UINT64 l1Size = 32 * 1024;  // 32KB
        UINT64 l1Ways = 8;
        UINT64 l1Line = 64;
        UINT64 l2Size = 256 * 1024;  // 256KB
        UINT64 l2Ways = 16;
        UINT64 l2Line = 64;
        UINT64 l3Size = 8 * 1024 * 1024;  // 8MB
        UINT64 l3Ways = 16;
        UINT64 l3Line = 64;
    } cache;

    struct {
        UINT64 pgdSize = 512;
        UINT64 pudSize = 512;
        UINT64 pmdSize = 512;
        UINT64 pteSize = 512;
        bool pteCachable = false;
        bool tocEnabled = false;
        UINT64 tocSize = 0;  // Size of the table of contents (TOC) in bytes
    } pgtbl;

    std::string traceFile;  // Path to the trace file
    UINT64 batchSize =
        4096;  // Number of MEMREF entries to process in each batch

    UINT64 PhysicalMemBytes() const { return physMemGb * (1ULL << 30); }

    void Print(std::ostream& os = std::cout) const {
        os << "Simulation Configuration:\n"
           << "==============================\n"
           << "Trace File:          " << traceFile << "\n"
           << "Batch Size:          " << batchSize << " entries\n"
           << "Physical Memory:     " << physMemGb << " GB\n"
           << "L1 TLB:             " << tlb.l1Size << " entries, " << tlb.l1Ways
           << "-way\n"
           << "L2 TLB:             " << tlb.l2Size << " entries, " << tlb.l2Ways
           << "-way\n"
           << "Page Walk Cache (PGD): " << pwc.pgdSize << " entries, "
           << pwc.pgdWays << "-way\n"
           << "Page Walk Cache (PUD): " << pwc.pudSize << " entries, "
           << pwc.pudWays << "-way\n"
           << "Page Walk Cache (PMD): " << pwc.pmdSize << " entries, "
           << pwc.pmdWays << "-way\n"
           << "L1 Cache:           " << cache.l1Size / 1024 << "KB, "
           << cache.l1Ways << "-way, " << cache.l1Line << "B line\n"
           << "L2 Cache:           " << cache.l2Size / 1024 << "KB, "
           << cache.l2Ways << "-way, " << cache.l2Line << "B line\n"
           << "L3 Cache:           " << cache.l3Size / (1024 * 1024) << "MB, "
           << cache.l3Ways << "-way, " << cache.l3Line << "B line\n"
           << "PTE Cacheable:      " << (pgtbl.pteCachable ? "true" : "false")
           << "\n"
           << "PGD Size:           " << pgtbl.pgdSize << " entries\n"
           << "PUD Size:           " << pgtbl.pudSize << " entries\n"
           << "PMD Size:           " << pgtbl.pmdSize << " entries\n"
           << "PTE Size:           " << pgtbl.pteSize << " entries\n"
           << "TOC Enabled:        " << (pgtbl.tocEnabled ? "true" : "false")
           << "\n"
           << "TOC Size:          " << pgtbl.tocSize << "\n";
    }
};

// Stats for translation paths
struct TranslationStats {
    UINT64 l1TlbHits = 0;           // Translations satisfied by L1 TLB
    UINT64 l2TlbHits = 0;           // Translations satisfied by L2 TLB
    UINT64 pmdCacheHits = 0;        // Translations requiring PMD PWC
    UINT64 pudCacheHits = 0;        // Translations requiring PUD PWC
    UINT64 pgdCacheHits = 0;        // Translations requiring PGD PWC
    UINT64 fullWalks = 0;           // Translations requiring full page walk
    UINT64 pageWalkMemAccess = 0;   // Memory Access during page walk
    UINT64 pteDataCacheHits = 0;    // Hits in data cache during walk
    UINT64 pteDataCacheMisses = 0;  // Miss

    UINT64 l2DataCacheAccess = 0;  // Data cache accesses during walk
    UINT64 l2DataCacheHits = 0;    // Hits in data cache during walk
    UINT64 l3DataCacheAccess = 0;  // Data cache accesses during walk
    UINT64 l3DataCacheHits = 0;    // Hits in data cache during walk

    TranslationStats() = default;

    UINT64 GetTotalTranslation() const {
        return l1TlbHits + l2TlbHits + pmdCacheHits + pudCacheHits +
               pgdCacheHits + fullWalks;
    }

    void PrintTranslationStats(std::ostream& os) const {
        UINT64 totalTranslations = this->GetTotalTranslation();
        os << "\nTranslation Path Statistics:" << std::endl;
        os << "===========================" << std::endl;
        os << std::left << std::setw(30) << "Path" << std::right
           << std::setw(15) << "Count" << std::setw(15) << "Percentage"
           << std::endl;
        os << std::string(60, '-') << std::endl;

        os << std::left << std::setw(30) << "L1 TLB Hit" << std::right
           << std::setw(15) << this->l1TlbHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->l1TlbHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "L2 TLB Hit" << std::right
           << std::setw(15) << this->l2TlbHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->l2TlbHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "PMD PWC Hit" << std::right
           << std::setw(15) << this->pmdCacheHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->pmdCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "PUD PWC Hit" << std::right
           << std::setw(15) << this->pudCacheHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->pudCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "PGD PWC Hit" << std::right
           << std::setw(15) << this->pgdCacheHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->pgdCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "Full Page Walk" << std::right
           << std::setw(15) << this->fullWalks << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->fullWalks / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "Total Translations" << std::right
           << std::setw(15) << totalTranslations << std::setw(15) << "100.00%"
           << std::endl;

        // Calculate TLB efficiency
        double tlbEfficiency = (double)(this->l1TlbHits + this->l2TlbHits) /
                               totalTranslations * 100.0;
        os << "\nTLB Efficiency: " << std::fixed << std::setprecision(2)
           << tlbEfficiency << "% (translations resolved by L1 or L2 TLB)"
           << std::endl;

        os << "Data Cache Stats during Translation:" << std::endl;
        os << "===========================" << std::endl;
        os << std::left << std::setw(30) << "PTE Data Cache Hits" << std::right
           << std::setw(15) << this->pteDataCacheHits << std::setw(15)
           << std::fixed << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->pteDataCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;
        os << std::left << std::setw(30) << "PTE Data Cache Misses"
           << std::right << std::setw(15) << this->pteDataCacheMisses
           << std::setw(15) << std::fixed << std::setprecision(2)
           << (totalTranslations > 0 ? (double)this->pteDataCacheMisses /
                                           totalTranslations * 100.0
                                     : 0.0)
           << "%" << std::endl;
        os << std::left << std::setw(30) << "L2 Data Cache Access" << std::right
           << std::setw(15) << this->l2DataCacheAccess << std::setw(15)
           << std::fixed << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->l2DataCacheAccess / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;
        os << std::left << std::setw(30) << "L2 Data Cache Hits" << std::right
           << std::setw(15) << this->l2DataCacheHits << std::setw(15)
           << std::fixed << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->l2DataCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;
        os << std::left << std::setw(30) << "L3 Data Cache Access" << std::right
           << std::setw(15) << this->l3DataCacheAccess << std::setw(15)
           << std::fixed << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->l3DataCacheAccess / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;
        os << std::left << std::setw(30) << "L3 Data Cache Hits" << std::right
           << std::setw(15) << this->l3DataCacheHits << std::setw(15)
           << std::fixed << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)this->l3DataCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;
        os << std::string(60, '-') << std::endl;
    }
};