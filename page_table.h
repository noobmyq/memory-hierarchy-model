#pragma once

#include <cassert>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include "common.h"
#include "data_cache.h"
#include "physical_memory.h"
#include "pwc.h"
#include "tlb.h"

// Page Table Entry
struct PageTableEntry {
    UINT64 present : 1;   // Present bit
    UINT64 writable : 1;  // Writable bit
    UINT64 user : 1;      // User accessible
    UINT64 pfn : 52;      // Physical Frame Number (40 bits used)
    UINT64 unused : 9;    // Unused bits

    PageTableEntry() : present(0), writable(0), user(0), pfn(0), unused(0) {}
};

// Page Table (4-level) with PWCs and two-level TLB
class PageTable {
   private:
    std::unordered_map<UINT64, std::unique_ptr<PageTableEntry[]>> pageTables_;
    UINT64 cr3_;                 // Page table base register (points to PGD)
    PhysicalMemory& physMem_;    // Reference to physical memory
    CacheHierarchy& dataCache_;  // Reference to data cache
    bool isPteCachable_;         // PTE cacheable flag
    // Two-level TLB
    TLB l1Tlb_;  // L1 TLB (smaller, faster)
    TLB l2Tlb_;  // L2 TLB (larger, slower)

    // page table set up
    const UINT64 pgdEntryNum_;
    const UINT64 pudEntryNum_;
    const UINT64 pmdEntryNum_;
    const UINT64 pteEntryNum_;

    const int pteIndexShift_;
    const int pmdShift_;
    const int pudShift_;
    const int pgdShift_;

    const UINT64 pteMask_ = pteEntryNum_ - 1;
    const UINT64 pmdMask_ = pmdEntryNum_ - 1;
    const UINT64 pudMask_ = pudEntryNum_ - 1;
    const UINT64 pgdMask_ = pgdEntryNum_ - 1;

    // Page Walk Caches for different levels
    PageWalkCache pgdPwc_;  // PML4E cache (PGD)
    PageWalkCache pudPwc_;  // PDPTE cache (PUD)
    PageWalkCache pmdPwc_;  // PDE cache (PMD)

    // Statistics for page table translation
    TranslationStats translationStats_;
    // Per-level statistics
    struct PageTableLevelStats {
        std::string name;    // Level name
        UINT64 accesses;     // Number of times this level was accessed
        UINT64 allocations;  // Number of tables allocated at this level
        UINT64 entries;      // Number of entries used at this level
        UINT64 size;         // Size of the table at this level

        PageTableLevelStats(const std::string& levelName, UINT64 tableSize)
            : name(levelName),
              accesses(0),
              allocations(0),
              entries(0),
              size(tableSize) {}
    };

    // Statistics for each level
    PageTableLevelStats pgdStats_;
    PageTableLevelStats pudStats_;
    PageTableLevelStats pmdStats_;
    PageTableLevelStats pteStats_;

   public:
    PageTable(PhysicalMemory& physicalMemory, CacheHierarchy& dataCache,
              bool isPteCachable = true, UINT64 l1TlbSize = 64,
              UINT64 l1TlbWays = 4, UINT64 l2TlbSize = 1024,
              UINT64 l2TlbWays = 8, UINT64 pgdPwcSize = 16,
              UINT64 pgdPwcWays = 4, UINT64 pudPwcSize = 16,
              UINT64 pudPwcWays = 4, UINT64 pmdPwcSize = 16,
              UINT64 pmdPwcWays = 4, UINT64 pgdEntryNum = 512,
              UINT64 pudEntryNum = 512, UINT64 pmdEntryNum = 512,
              UINT64 pteEntryNum = 512, bool tocEnabled = false,
              UINT64 tocSize = 0)
        : physMem_(physicalMemory),
          dataCache_(dataCache),
          isPteCachable_(isPteCachable),
          l1Tlb_("L1 TLB", l1TlbSize, l1TlbWays),
          l2Tlb_("L2 TLB", l2TlbSize, l2TlbWays),
          pgdEntryNum_(pgdEntryNum),
          pudEntryNum_(pudEntryNum),
          pmdEntryNum_(pmdEntryNum),
          pteEntryNum_(pteEntryNum),
          pteIndexShift_(kPageShift),
          pmdShift_(pteIndexShift_ + StaticLog2(pteEntryNum)),
          pudShift_(pmdShift_ + StaticLog2(pmdEntryNum)),
          pgdShift_(pudShift_ + StaticLog2(pudEntryNum)),
          pgdPwc_("PML4E Cache (PGD)", pgdPwcSize, pgdPwcWays, pgdShift_, 47),
          pudPwc_("PDPTE Cache (PUD)", pudPwcSize, pudPwcWays, pudShift_, 47),
          pmdPwc_("PDE Cache (PMD)", pmdPwcSize, pmdPwcWays, pmdShift_, 47),
          translationStats_(),
          pgdStats_("PGD (Page Global Directory)", pgdEntryNum),
          pudStats_("PUD (Page Upper Directory)", pudEntryNum),
          pmdStats_("PMD (Page Middle Directory)", pmdEntryNum),
          pteStats_("PTE (Page Table Entry)", pteEntryNum) {
        // Allocate the root page table (PGD)
        cr3_ = physMem_.AllocateFrame() * kMemTracePageSize;
        pageTables_[cr3_] = std::make_unique<PageTableEntry[]>(pgdEntryNum);
        // memset all entries to 0
        std::fill_n(pageTables_[cr3_].get(), pgdEntryNum, PageTableEntry());
        pgdStats_.allocations++;
        // assert that the page table entry is power of 2
        assert((pgdEntryNum & (pgdEntryNum - 1)) == 0);
        assert((pudEntryNum & (pudEntryNum - 1)) == 0);
        assert((pmdEntryNum & (pmdEntryNum - 1)) == 0);
        assert((pteEntryNum & (pteEntryNum - 1)) == 0);
        assert(pgdShift_ + StaticLog2(pgdEntryNum) == 48);

        if (tocEnabled) {
            assert(tocSize > 0 && (tocSize & (tocSize - 1)) == 0);
            pgdPwc_.SetTocEnabled(true);
            pgdPwc_.SetTocSize(tocSize);
            pudPwc_.SetTocEnabled(true);
            pudPwc_.SetTocSize(tocSize);
            pmdPwc_.SetTocEnabled(true);
            pmdPwc_.SetTocSize(tocSize);
        } else {
            assert(tocSize == 0);
        }
    }

    // Get indexes into the page tables for a given virtual address
    UINT64 GetPgdIndex(ADDRINT vaddr) const {
        return (vaddr >> pgdShift_) & pgdMask_;
    }

    UINT64 GetPudIndex(ADDRINT vaddr) const {
        return (vaddr >> pudShift_) & pudMask_;
    }

    UINT64 GetPmdIndex(ADDRINT vaddr) const {
        return (vaddr >> pmdShift_) & pmdMask_;
    }

    UINT64 GetPteIndex(ADDRINT vaddr) const {
        return (vaddr >> pteIndexShift_) & pteMask_;
    }

    UINT64 GetOffset(ADDRINT vaddr) const { return vaddr & kPageMask; }

    // Complete translation from PTE level - used by PMD PWC hit path
    ADDRINT CompletePmdCacheHit(ADDRINT vaddr, UINT64 pteTablePfn) {
        UINT64 pteAddr = pteTablePfn << kPageShift;
        UINT64 pteIndex = GetPteIndex(vaddr);
        UINT64 offset = GetOffset(vaddr);

        // Access the PTE table
        // the entry size is not sure to be 8Byte
        UINT64 entrySize = kMemTracePageSize / pteEntryNum_;
        UINT64 pteEntryAddr = pteAddr + (pteIndex * entrySize);

        // Cache Lookup for PTE entry (if cacheable)
        bool hit = false;
        UINT64 pteEntryValue = 0;
        if (isPteCachable_)
            hit = dataCache_.TranslateLookup(pteEntryAddr, pteEntryValue,
                                             translationStats_);
        PageTableEntry& pteEntry = pageTables_[pteAddr][pteIndex];

        // Allocate physical page if not present
        if (!pteEntry.present) {
            pteEntry.present = 1;
            pteEntry.writable = 1;
            pteEntry.pfn = physMem_.AllocateFrame();
            pteStats_.entries++;
            // assert(!hit);
        }

        // Handle memory/cache access
        if (hit) {
            translationStats_.pteDataCacheHits++;
        } else {
            // Either cache miss or non-cacheable PTE
            if (isPteCachable_)
                translationStats_.pteDataCacheMisses++;
            translationStats_.pageWalkMemAccess++;
            pteStats_.accesses++;
        }

        // Return physical address
        return (pteEntry.pfn << kPageShift) | offset;
    }

    // Complete translation from PMD level - used by PUD PWC hit path
    ADDRINT CompletePudCacheHit(ADDRINT vaddr, UINT64 pmdTablePfn) {
        UINT64 pmdAddr = pmdTablePfn << kPageShift;
        UINT64 pmdIndex = GetPmdIndex(vaddr);

        // Access the PMD table
        UINT64 entrySize = kMemTracePageSize / pmdEntryNum_;
        UINT64 pmdEntryAddr = pmdAddr + (pmdIndex * entrySize);
        // first access data cache
        UINT64 pmdEntryValue = 0;
        // Cache Lookup for PMD entry (if cacheable)
        bool hit = false;
        if (isPteCachable_)
            hit = dataCache_.TranslateLookup(pmdEntryAddr, pmdEntryValue,
                                             translationStats_);

        PageTableEntry& pmdEntry = pageTables_[pmdAddr][pmdIndex];
        // Allocate PTE if not present
        if (!pmdEntry.present) {
            pmdEntry.present = 1;
            pmdEntry.writable = 1;
            pmdEntry.pfn = physMem_.AllocateFrame();
            UINT64 pteAddr = pmdEntry.pfn * kMemTracePageSize;
            pageTables_[pteAddr] =
                std::make_unique<PageTableEntry[]>(pteEntryNum_);
            // memset all entries to 0
            std::fill_n(pageTables_[pteAddr].get(), pteEntryNum_,
                        PageTableEntry());
            pteStats_.allocations++;
            pmdStats_.entries++;
            // assert(!hit);
        }
        if (hit) {
            translationStats_.pteDataCacheHits++;
        } else {
            if (isPteCachable_) {
                translationStats_.pteDataCacheMisses++;
            }
            translationStats_.pageWalkMemAccess++;
            pmdStats_.accesses++;
        }

        // Insert into PMD PWC
        pmdPwc_.Insert(vaddr, pmdEntry.pfn);

        // Complete the translation
        return CompletePmdCacheHit(vaddr, pmdEntry.pfn);
    }

    // Complete translation from PUD level - used by PGD PWC hit path
    ADDRINT CompletePgdCacheHit(ADDRINT vaddr, UINT64 pudTablePfn) {
        UINT64 pudAddr = pudTablePfn << kPageShift;
        UINT64 pudIndex = GetPudIndex(vaddr);

        // Access the PUD table
        UINT64 entrySize = kMemTracePageSize / pudEntryNum_;
        UINT64 pudEntryAddr = pudAddr + (pudIndex * entrySize);
        // first access data cache
        UINT64 pudEntryValue = 0;
        bool hit = false;
        // Cache Lookup for PUD entry (if cacheable)
        if (isPteCachable_)
            hit = dataCache_.TranslateLookup(pudEntryAddr, pudEntryValue,
                                             translationStats_);
        PageTableEntry& pudEntry = pageTables_[pudAddr][pudIndex];
        // Allocate PMD if not present
        if (!pudEntry.present) {
            pudEntry.present = 1;
            pudEntry.writable = 1;
            pudEntry.pfn = physMem_.AllocateFrame();
            UINT64 pmdAddr = pudEntry.pfn * kMemTracePageSize;
            pageTables_[pmdAddr] =
                std::make_unique<PageTableEntry[]>(pmdEntryNum_);
            // memset all entries to 0
            std::fill_n(pageTables_[pmdAddr].get(), pmdEntryNum_,
                        PageTableEntry());
            pmdStats_.allocations++;
            pudStats_.entries++;
        }

        if (hit) {
            translationStats_.pteDataCacheHits++;
        } else {
            if (isPteCachable_) {
                translationStats_.pteDataCacheMisses++;
            }
            translationStats_.pageWalkMemAccess++;
            pudStats_.accesses++;
        }

        // Insert into PUD PWC
        pudPwc_.Insert(vaddr, pudEntry.pfn);

        // Complete the translation
        return CompletePudCacheHit(vaddr, pudEntry.pfn);
    }

    // Complete a full page table walk
    ADDRINT CompleteFullWalk(ADDRINT vaddr) {
        // Step 1: Get PGD entry
        UINT64 pgdIndex = GetPgdIndex(vaddr);
        UINT64 pgdAddr = cr3_ + (pgdIndex * sizeof(PageTableEntry));
        // first access data cache
        UINT64 pgdEntryValue = 0;
        bool hit = false;
        // Cache Lookup for PGD entry (if cacheable)
        if (isPteCachable_)
            hit = dataCache_.TranslateLookup(pgdAddr, pgdEntryValue,
                                             translationStats_);
        PageTableEntry& pgdEntry = pageTables_[cr3_][pgdIndex];
        // Allocate PUD if not present
        if (!pgdEntry.present) {
            pgdEntry.present = 1;
            pgdEntry.writable = 1;
            pgdEntry.pfn = physMem_.AllocateFrame();
            UINT64 pudAddr = pgdEntry.pfn * kMemTracePageSize;
            pageTables_[pudAddr] =
                std::make_unique<PageTableEntry[]>(pudEntryNum_);
            // memset all entries to 0
            std::fill_n(pageTables_[pudAddr].get(), pudEntryNum_,
                        PageTableEntry());
            pudStats_.allocations++;
            pgdStats_.entries++;
        }
        if (hit) {
            translationStats_.pteDataCacheHits++;
        } else {
            if (isPteCachable_) {
                translationStats_.pteDataCacheMisses++;
            }
            translationStats_.pageWalkMemAccess++;
            pgdStats_.accesses++;
        }

        // Insert into PGD PWC
        pgdPwc_.Insert(vaddr, pgdEntry.pfn);

        // Continue with PUD level
        return CompletePgdCacheHit(vaddr, pgdEntry.pfn);
    }

    // Translate a virtual address to physical address
    ADDRINT Translate(ADDRINT vaddr) {
        // Extract the virtual page number and page offset
        UINT64 vpn = vaddr >> kPageShift;
        UINT64 offset = GetOffset(vaddr);

        // 1. Check L1 TLB first (fastest)
        UINT64 pfn;
        if (l1Tlb_.Lookup(vpn, pfn)) {
            translationStats_.l1TlbHits++;
            // L1 TLB hit - combine PFN with offset
            return (pfn << kPageShift) | offset;
        }

        // 2. L1 TLB miss - check L2 TLB
        if (l2Tlb_.Lookup(vpn, pfn)) {
            translationStats_.l2TlbHits++;

            // L2 TLB hit - update L1 TLB with the translation
            l1Tlb_.Insert(vpn, pfn);

            // Combine PFN with offset
            return (pfn << kPageShift) | offset;
        }

        // 3. L2 TLB miss - check PMD PWC (maps VA[47:21] to PTE table PFN)
        UINT64 pteTablePfn;
        if (pmdPwc_.Lookup(vaddr, pteTablePfn)) {
            translationStats_.pmdCacheHits++;
            ADDRINT paddr = CompletePmdCacheHit(vaddr, pteTablePfn);

            // Update both TLBs with the translation
            pfn = paddr >> kPageShift;
            l1Tlb_.Insert(vpn, pfn);
            l2Tlb_.Insert(vpn, pfn);
            return paddr;
        }

        // 4. PMD PWC miss - check PUD PWC (maps VA[47:30] to PMD table PFN)
        UINT64 pmdTablePfn;
        if (pudPwc_.Lookup(vaddr, pmdTablePfn)) {
            translationStats_.pudCacheHits++;
            ADDRINT paddr = CompletePudCacheHit(vaddr, pmdTablePfn);

            // Update both TLBs with the translation
            pfn = paddr >> kPageShift;
            l1Tlb_.Insert(vpn, pfn);
            l2Tlb_.Insert(vpn, pfn);
            return paddr;
        }

        // 5. PUD PWC miss - check PGD PWC (maps VA[47:39] to PUD table PFN)
        UINT64 pudTablePfn;
        if (pgdPwc_.Lookup(vaddr, pudTablePfn)) {
            translationStats_.pgdCacheHits++;
            ADDRINT paddr = CompletePgdCacheHit(vaddr, pudTablePfn);

            // Update both TLBs with the translation
            pfn = paddr >> kPageShift;
            l1Tlb_.Insert(vpn, pfn);
            l2Tlb_.Insert(vpn, pfn);
            return paddr;
        }

        // 6. Full page table walk needed
        translationStats_.fullWalks++;
        ADDRINT paddr = CompleteFullWalk(vaddr);

        // Update both TLBs with the translation
        pfn = paddr >> kPageShift;
        l1Tlb_.Insert(vpn, pfn);
        l2Tlb_.Insert(vpn, pfn);
        return paddr;
    }

    // Print detailed page table and cache statistics
    void PrintDetailedStats(std::ostream& os) const {
        translationStats_.PrintTranslationStats(os);

        // Cache statistics
        os << "\nCache Statistics:" << '\n';
        os << "================" << '\n';
        os << std::left << std::setw(30) << "Cache" << std::setw(10)
           << "Entries" << std::setw(10) << "Sets" << std::setw(10) << "Ways"
           << std::right << std::setw(15) << "Accesses" << std::setw(15)
           << "Hits" << std::setw(15) << "Hit Rate" << '\n';
        os << std::string(105, '-') << '\n';

        // TLB stats
        os << std::left << std::setw(30) << l1Tlb_.GetName() << std::setw(10)
           << l1Tlb_.GetSize() << std::setw(10) << l1Tlb_.GetNumSets()
           << std::setw(10) << l1Tlb_.GetNumWays() << std::right
           << std::setw(15) << l1Tlb_.GetAccesses() << std::setw(15)
           << l1Tlb_.GetHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << l1Tlb_.GetHitRate() * 100.0 << "%"
           << '\n';

        os << std::left << std::setw(30) << l2Tlb_.GetName() << std::setw(10)
           << l2Tlb_.GetSize() << std::setw(10) << l2Tlb_.GetNumSets()
           << std::setw(10) << l2Tlb_.GetNumWays() << std::right
           << std::setw(15) << l2Tlb_.GetAccesses() << std::setw(15)
           << l2Tlb_.GetHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << l2Tlb_.GetHitRate() * 100.0 << "%"
           << '\n';

        // PWC stats
        os << std::left << std::setw(30) << pgdPwc_.GetName() << std::setw(10)
           << pgdPwc_.GetSize() << std::setw(10) << pgdPwc_.GetNumSets()
           << std::setw(10) << pgdPwc_.GetNumWays() << std::right
           << std::setw(15) << pgdPwc_.GetAccesses() << std::setw(15)
           << pgdPwc_.GetHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << pgdPwc_.GetHitRate() * 100.0 << "%"
           << '\n';

        os << std::left << std::setw(30) << pudPwc_.GetName() << std::setw(10)
           << pudPwc_.GetSize() << std::setw(10) << pudPwc_.GetNumSets()
           << std::setw(10) << pudPwc_.GetNumWays() << std::right
           << std::setw(15) << pudPwc_.GetAccesses() << std::setw(15)
           << pudPwc_.GetHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << pudPwc_.GetHitRate() * 100.0 << "%"
           << '\n';

        os << std::left << std::setw(30) << pmdPwc_.GetName() << std::setw(10)
           << pmdPwc_.GetSize() << std::setw(10) << pmdPwc_.GetNumSets()
           << std::setw(10) << pmdPwc_.GetNumWays() << std::right
           << std::setw(15) << pmdPwc_.GetAccesses() << std::setw(15)
           << pmdPwc_.GetHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << pmdPwc_.GetHitRate() * 100.0 << "%"
           << '\n';

        os << "\nVirtual Address Bit Ranges Used for PWC Tags:" << '\n';
        os << std::left << std::setw(30) << pgdPwc_.GetName() << "["
           << pgdPwc_.GetHighBit() << ":" << pgdPwc_.GetLowBit() << "]" << '\n';
        os << std::left << std::setw(30) << pudPwc_.GetName() << "["
           << pudPwc_.GetHighBit() << ":" << pudPwc_.GetLowBit() << "]" << '\n';
        os << std::left << std::setw(30) << pmdPwc_.GetName() << "["
           << pmdPwc_.GetHighBit() << ":" << pmdPwc_.GetLowBit() << "]" << '\n';

        // Page table statistics by level
        os << "\nPage Table Statistics by Level:" << '\n';
        os << "==============================" << '\n';

        // Header
        os << std::setw(30) << std::left << "Level" << std::setw(15)
           << std::right << "Accesses" << std::setw(15) << std::right
           << "Tables" << std::setw(15) << std::right << "Entries"
           << std::setw(15) << std::right << "Avg Fill %" << '\n';
        os << std::string(90, '-') << '\n';

        // Calculate and print stats for each level
        PrintLevelStats(os, pgdStats_);
        PrintLevelStats(os, pudStats_);
        PrintLevelStats(os, pmdStats_);
        PrintLevelStats(os, pteStats_);

        os << "\nTotal page tables: " << pageTables_.size() << '\n';
        os << "Total memory for page tables: "
           << (pageTables_.size() * kMemTracePageSize) / (1024.0 * 1024.0)
           << " MB" << '\n';
    }

    void PrintMemoryStats(std::ostream& os) const {
        os << "\nCache Access Statistics (from Page Table):\n";
        os << "=========================================\n";
        os << std::left << std::setw(35) << "Page Table Entry data Cache Hits"
           << std::right << std::setw(10) << translationStats_.pteDataCacheHits
           << "\n";
        os << std::left << std::setw(35) << "Page Table Entry data Cache Misses"
           << std::right << std::setw(10)
           << translationStats_.pteDataCacheMisses << "\n";
        os << std::left << std::setw(35) << "Page Walk Memory Accesses"
           << std::right << std::setw(10) << translationStats_.pageWalkMemAccess
           << "\n";
        os << std::left << std::setw(35) << "Page Table Entry Cache hits ratio"
           << std::right << std::setw(10)
           << (translationStats_.pteDataCacheHits +
                           translationStats_.pteDataCacheMisses >
                       0
                   ? (double)translationStats_.pteDataCacheHits /
                         (translationStats_.pteDataCacheHits +
                          translationStats_.pteDataCacheMisses) *
                         100.0
                   : 0.0)
           << "%\n";
    }

   private:
    // Helper to print stats for a page table level
    void PrintLevelStats(std::ostream& os,
                         const PageTableLevelStats& stats) const {
        double avgFill = 0.0;
        if (stats.allocations > 0) {
            avgFill = (static_cast<double>(stats.entries) / stats.allocations) /
                      stats.size * 100.0;
        }

        os << std::setw(30) << std::left << stats.name << std::setw(15)
           << std::right << stats.accesses << std::setw(15) << std::right
           << stats.allocations << std::setw(15) << std::right << stats.entries
           << std::setw(15) << std::right << std::fixed << std::setprecision(2)
           << avgFill << '\n';
    }

   public:
    // Get statistics
    UINT64 GetNumPageTables() const { return pageTables_.size(); }

    // TLB statistics
    double GetL1TlbHitRate() const { return l1Tlb_.GetHitRate(); }
    double GetL2TlbHitRate() const { return l2Tlb_.GetHitRate(); }
    UINT64 GetL1TlbAccesses() const { return l1Tlb_.GetAccesses(); }
    UINT64 GetL2TlbAccesses() const { return l2Tlb_.GetAccesses(); }
    UINT64 GetL1TlbHits() const { return l1Tlb_.GetHits(); }
    UINT64 GetL2TlbHits() const { return l2Tlb_.GetHits(); }

    // Overall TLB efficiency
    double GetTlbEfficiency() const {
        UINT64 totalTranslations = translationStats_.GetTotalTranslation();
        return totalTranslations > 0 ? (double)(translationStats_.l1TlbHits +
                                                translationStats_.l2TlbHits) /
                                           totalTranslations
                                     : 0.0;
    }

    // Page walk statistics
    UINT64 GetPageTableWalks() const { return translationStats_.fullWalks; }
    UINT64 GetFullWalks() const { return translationStats_.fullWalks; }
    UINT64 GetPgdCacheHits() const { return translationStats_.pgdCacheHits; }
    UINT64 GetPudCacheHits() const { return translationStats_.pudCacheHits; }
    UINT64 GetPmdCacheHits() const { return translationStats_.pmdCacheHits; }
    double GetPgdCacheHitRate() const { return pgdPwc_.GetHitRate(); }
    double GetPudCacheHitRate() const { return pudPwc_.GetHitRate(); }
    double GetPmdCacheHitRate() const { return pmdPwc_.GetHitRate(); }
};