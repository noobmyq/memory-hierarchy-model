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
    std::unordered_map<UINT64, std::unique_ptr<PageTableEntry[]>> pageTables;
    UINT64 cr3;                 // Page table base register (points to PGD)
    PhysicalMemory& physMem;    // Reference to physical memory
    CacheHierarchy& dataCache;  // Reference to data cache
    bool isPteCachable;         // PTE cacheable flag
    // Two-level TLB
    TLB l1Tlb;  // L1 TLB (smaller, faster)
    TLB l2Tlb;  // L2 TLB (larger, slower)

    // page table set up
    const size_t pgdEntrySize;
    const size_t pudEntrySize;
    const size_t pmdEntrySize;
    const size_t pteEntrySize;

    const int PTE_INDEX_SHIFT;
    const int PMD_SHIFT;
    const int PUD_SHIFT;
    const int PGD_SHIFT;

    const UINT64 PTE_MASK = pteEntrySize - 1;
    const UINT64 PMD_MASK = pmdEntrySize - 1;
    const UINT64 PUD_MASK = pudEntrySize - 1;
    const UINT64 PGD_MASK = pgdEntrySize - 1;

    // Page Walk Caches for different levels
    PageWalkCache pgdPwc;  // PML4E cache (PGD)
    PageWalkCache pudPwc;  // PDPTE cache (PUD)
    PageWalkCache pmdPwc;  // PDE cache (PMD)

    // Stats for translation paths
    UINT64 l1TlbHits;           // Translations satisfied by L1 TLB
    UINT64 l2TlbHits;           // Translations satisfied by L2 TLB
    UINT64 pmdCacheHits;        // Translations requiring PMD PWC
    UINT64 pudCacheHits;        // Translations requiring PUD PWC
    UINT64 pgdCacheHits;        // Translations requiring PGD PWC
    UINT64 fullWalks;           // Translations requiring full page walk
    size_t pageWalkMemAccess;   // 页表遍历导致的实际内存访问
    size_t pteDataCacheHits;    // 页表项缓存命中次数
    size_t pteDataCacheMisses;  // 页表项缓存未命中次数

    // Per-level statistics
    struct PageTableLevelStats {
        std::string name;    // Level name
        UINT64 accesses;     // Number of times this level was accessed
        UINT64 allocations;  // Number of tables allocated at this level
        UINT64 entries;      // Number of entries used at this level
        UINT64 size;         // Size of the table at this level

        PageTableLevelStats(const std::string& levelName, size_t tableSize)
            : name(levelName),
              accesses(0),
              allocations(0),
              entries(0),
              size(tableSize) {}
    };

    // Statistics for each level
    PageTableLevelStats pgdStats;
    PageTableLevelStats pudStats;
    PageTableLevelStats pmdStats;
    PageTableLevelStats pteStats;

   public:
    PageTable(PhysicalMemory& physicalMemory, CacheHierarchy& dataCache,
              bool isPteCachable = true, size_t l1TlbSize = 64,
              size_t l1TlbWays = 4, size_t l2TlbSize = 1024,
              size_t l2TlbWays = 8, size_t pgdPwcSize = 16,
              size_t pgdPwcWays = 4, size_t pudPwcSize = 16,
              size_t pudPwcWays = 4, size_t pmdPwcSize = 16,
              size_t pmdPwcWays = 4, size_t pgdEntrySize = 512,
              size_t pudEntrySize = 512, size_t pmdEntrySize = 512,
              size_t pteEntrySize = 512)
        : physMem(physicalMemory),
          dataCache(dataCache),
          isPteCachable(isPteCachable),
          l1Tlb("L1 TLB", l1TlbSize, l1TlbWays),
          l2Tlb("L2 TLB", l2TlbSize, l2TlbWays),
          pgdEntrySize(pgdEntrySize),
          pudEntrySize(pudEntrySize),
          pmdEntrySize(pmdEntrySize),
          pteEntrySize(pteEntrySize),
          PTE_INDEX_SHIFT(PAGE_SHIFT),
          PMD_SHIFT(PTE_INDEX_SHIFT + static_log2(pteEntrySize)),
          PUD_SHIFT(PMD_SHIFT + static_log2(pmdEntrySize)),
          PGD_SHIFT(PUD_SHIFT + static_log2(pudEntrySize)),
          pgdPwc("PML4E Cache (PGD)", pgdPwcSize, pgdPwcWays, PGD_SHIFT, 47),
          pudPwc("PDPTE Cache (PUD)", pudPwcSize, pudPwcWays, PUD_SHIFT, 47),
          pmdPwc("PDE Cache (PMD)", pmdPwcSize, pmdPwcWays, PMD_SHIFT, 47),
          l1TlbHits(0),
          l2TlbHits(0),
          pmdCacheHits(0),
          pudCacheHits(0),
          pgdCacheHits(0),
          fullWalks(0),
          pageWalkMemAccess(0),
          pteDataCacheHits(0),
          pteDataCacheMisses(0),
          pgdStats("PGD (Page Global Directory)", pgdEntrySize),
          pudStats("PUD (Page Upper Directory)", pudEntrySize),
          pmdStats("PMD (Page Middle Directory)", pmdEntrySize),
          pteStats("PTE (Page Table Entry)", pteEntrySize) {
        // Allocate the root page table (PGD)
        cr3 = physMem.allocateFrame() * MEMTRACE_PAGE_SIZE;
        pageTables[cr3] = std::make_unique<PageTableEntry[]>(pgdEntrySize);
        pgdStats.allocations++;
        // assert that the page table entry is power of 2
        assert((pgdEntrySize & (pgdEntrySize - 1)) == 0);
        assert((pudEntrySize & (pudEntrySize - 1)) == 0);
        assert((pmdEntrySize & (pmdEntrySize - 1)) == 0);
        assert((pteEntrySize & (pteEntrySize - 1)) == 0);
        assert(PGD_SHIFT + static_log2(pgdEntrySize) == 48);
    }

    // Get indexes into the page tables for a given virtual address
    UINT32 getPgdIndex(ADDRINT vaddr) const {
        return (vaddr >> PGD_SHIFT) & PGD_MASK;
    }

    UINT32 getPudIndex(ADDRINT vaddr) const {
        return (vaddr >> PUD_SHIFT) & PUD_MASK;
    }

    UINT32 getPmdIndex(ADDRINT vaddr) const {
        return (vaddr >> PMD_SHIFT) & PMD_MASK;
    }

    UINT32 getPteIndex(ADDRINT vaddr) const {
        return (vaddr >> PTE_INDEX_SHIFT) & PTE_MASK;
    }

    UINT32 getOffset(ADDRINT vaddr) const { return vaddr & PAGE_MASK; }

    // Complete translation from PTE level - used by PMD PWC hit path
    ADDRINT completePmdCacheHit(ADDRINT vaddr, UINT64 pteTablePfn) {
        UINT64 pteAddr = pteTablePfn << PAGE_SHIFT;
        UINT32 pteIndex = getPteIndex(vaddr);
        UINT32 offset = getOffset(vaddr);

        // Access the PTE table
        UINT64 pteEntryAddr = pteAddr + (pteIndex * sizeof(PageTableEntry));

        // Cache lookup for PTE entry (if cacheable)
        bool hit = false;
        UINT64 pteEntryValue = 0;
        if (isPteCachable)
            hit = dataCache.translate_lookup(pteEntryAddr, pteEntryValue);
        PageTableEntry& pteEntry = pageTables[pteAddr][pteIndex];

        // Allocate physical page if not present
        if (!pteEntry.present) {
            pteEntry.present = 1;
            pteEntry.writable = 1;
            pteEntry.pfn = physMem.allocateFrame();
            pteStats.entries++;
            // assert(!hit);
        }

        // Handle memory/cache access
        if (hit) {
            pteDataCacheHits++;
            // assert(*((UINT64*)&pteEntry) == pteEntryValue);
        } else {
            // Either cache miss or non-cacheable PTE
            if (isPteCachable) {
                dataCache.translate_access(pteEntryAddr, *((UINT64*)&pteEntry),
                                           true);
                pteDataCacheMisses++;
                dataCache.memAccessCount++;
            }
            pageWalkMemAccess++;
            pteStats.accesses++;
        }

        // Return physical address
        return (pteEntry.pfn << PAGE_SHIFT) | offset;
    }

    // Complete translation from PMD level - used by PUD PWC hit path
    ADDRINT completePudCacheHit(ADDRINT vaddr, UINT64 pmdTablePfn) {
        UINT64 pmdAddr = pmdTablePfn << PAGE_SHIFT;
        UINT32 pmdIndex = getPmdIndex(vaddr);

        // Access the PMD table
        UINT64 pmdEntryAddr = pmdAddr + (pmdIndex * sizeof(PageTableEntry));
        // first access data cache
        UINT64 pmdEntryValue = 0;
        // Cache lookup for PMD entry (if cacheable)
        bool hit = false;
        if (isPteCachable)
            hit = dataCache.translate_lookup(pmdEntryAddr, pmdEntryValue);

        PageTableEntry& pmdEntry = pageTables[pmdAddr][pmdIndex];
        // Allocate PTE if not present
        if (!pmdEntry.present) {
            pmdEntry.present = 1;
            pmdEntry.writable = 1;
            pmdEntry.pfn = physMem.allocateFrame();
            UINT64 pteAddr = pmdEntry.pfn * MEMTRACE_PAGE_SIZE;
            pageTables[pteAddr] =
                std::make_unique<PageTableEntry[]>(pteEntrySize);
            pteStats.allocations++;
            pmdStats.entries++;
            // assert(!hit);
        }
        if (hit) {
            pteDataCacheHits++;
            // assert(*((UINT64*)&pmdEntry) == pmdEntryValue);
        } else {
            if (isPteCachable) {
                dataCache.translate_access(pmdEntryAddr, *((UINT64*)&pmdEntry),
                                           true);
                pteDataCacheMisses++;
                dataCache.memAccessCount++;
            }
            pageWalkMemAccess++;
            pmdStats.accesses++;
        }

        // Insert into PMD PWC
        pmdPwc.insert(vaddr, pmdEntry.pfn);

        // Complete the translation
        return completePmdCacheHit(vaddr, pmdEntry.pfn);
    }

    // Complete translation from PUD level - used by PGD PWC hit path
    ADDRINT completePgdCacheHit(ADDRINT vaddr, UINT64 pudTablePfn) {
        UINT64 pudAddr = pudTablePfn << PAGE_SHIFT;
        UINT32 pudIndex = getPudIndex(vaddr);

        // Access the PUD table
        UINT64 pudEntryAddr = pudAddr + (pudIndex * sizeof(PageTableEntry));
        // first access data cache
        UINT64 pudEntryValue = 0;
        bool hit = false;
        // Cache lookup for PUD entry (if cacheable)
        if (isPteCachable)
            hit = dataCache.translate_lookup(pudEntryAddr, pudEntryValue);
        PageTableEntry& pudEntry = pageTables[pudAddr][pudIndex];
        // Allocate PMD if not present
        if (!pudEntry.present) {
            pudEntry.present = 1;
            pudEntry.writable = 1;
            pudEntry.pfn = physMem.allocateFrame();
            UINT64 pmdAddr = pudEntry.pfn * MEMTRACE_PAGE_SIZE;
            pageTables[pmdAddr] =
                std::make_unique<PageTableEntry[]>(pmdEntrySize);
            pmdStats.allocations++;
            pudStats.entries++;
            // assert(!hit);
        }

        if (hit) {
            pteDataCacheHits++;
            // assert(*((UINT64*)&pudEntry) == pudEntryValue);
        } else {
            if (isPteCachable) {
                // Access the data cache
                dataCache.translate_access(pudEntryAddr, *((UINT64*)&pudEntry),
                                           true);
                pteDataCacheMisses++;
                dataCache.memAccessCount++;
            }
            pageWalkMemAccess++;
            pudStats.accesses++;
        }

        // Insert into PUD PWC
        pudPwc.insert(vaddr, pudEntry.pfn);

        // Complete the translation
        return completePudCacheHit(vaddr, pudEntry.pfn);
    }

    // Complete a full page table walk
    ADDRINT completeFullWalk(ADDRINT vaddr) {
        // Step 1: Get PGD entry
        UINT32 pgdIndex = getPgdIndex(vaddr);
        UINT64 pgdAddr = cr3 + (pgdIndex * sizeof(PageTableEntry));
        // first access data cache
        UINT64 pgdEntryValue = 0;
        bool hit = false;
        // Cache lookup for PGD entry (if cacheable)
        if (isPteCachable)
            hit = dataCache.translate_lookup(pgdAddr, pgdEntryValue);
        PageTableEntry& pgdEntry = pageTables[cr3][pgdIndex];
        // Allocate PUD if not present
        if (!pgdEntry.present) {
            pgdEntry.present = 1;
            pgdEntry.writable = 1;
            pgdEntry.pfn = physMem.allocateFrame();
            UINT64 pudAddr = pgdEntry.pfn * MEMTRACE_PAGE_SIZE;
            pageTables[pudAddr] =
                std::make_unique<PageTableEntry[]>(pudEntrySize);
            pudStats.allocations++;
            pgdStats.entries++;
            // assert(!hit);
        }
        if (hit) {
            pteDataCacheHits++;
            // assert(*((UINT64*)&pgdEntry) == pgdEntryValue);
        } else {
            if (isPteCachable) {
                // Access the data cache
                dataCache.translate_access(pgdAddr, *((UINT64*)&pgdEntry),
                                           true);
                pteDataCacheMisses++;
                dataCache.memAccessCount++;
            }
            pageWalkMemAccess++;
            pgdStats.accesses++;
        }

        // Insert into PGD PWC
        pgdPwc.insert(vaddr, pgdEntry.pfn);

        // Continue with PUD level
        return completePgdCacheHit(vaddr, pgdEntry.pfn);
    }

    // Translate a virtual address to physical address
    ADDRINT translate(ADDRINT vaddr) {
        // Extract the virtual page number and page offset
        UINT64 vpn = vaddr >> PAGE_SHIFT;
        UINT32 offset = getOffset(vaddr);

        // 1. Check L1 TLB first (fastest)
        UINT64 pfn;
        if (l1Tlb.lookup(vpn, pfn)) {
            l1TlbHits++;
            // L1 TLB hit - combine PFN with offset
            return (pfn << PAGE_SHIFT) | offset;
        }

        // 2. L1 TLB miss - check L2 TLB
        if (l2Tlb.lookup(vpn, pfn)) {
            l2TlbHits++;

            // L2 TLB hit - update L1 TLB with the translation
            l1Tlb.insert(vpn, pfn);

            // Combine PFN with offset
            return (pfn << PAGE_SHIFT) | offset;
        }

        // 3. L2 TLB miss - check PMD PWC (maps VA[47:21] to PTE table PFN)
        UINT64 pteTablePfn;
        if (pmdPwc.lookup(vaddr, pteTablePfn)) {
            pmdCacheHits++;
            ADDRINT paddr = completePmdCacheHit(vaddr, pteTablePfn);

            // Update both TLBs with the translation
            pfn = paddr >> PAGE_SHIFT;
            l1Tlb.insert(vpn, pfn);
            l2Tlb.insert(vpn, pfn);
            return paddr;
        }

        // 4. PMD PWC miss - check PUD PWC (maps VA[47:30] to PMD table PFN)
        UINT64 pmdTablePfn;
        if (pudPwc.lookup(vaddr, pmdTablePfn)) {
            pudCacheHits++;
            ADDRINT paddr = completePudCacheHit(vaddr, pmdTablePfn);

            // Update both TLBs with the translation
            pfn = paddr >> PAGE_SHIFT;
            l1Tlb.insert(vpn, pfn);
            l2Tlb.insert(vpn, pfn);
            return paddr;
        }

        // 5. PUD PWC miss - check PGD PWC (maps VA[47:39] to PUD table PFN)
        UINT64 pudTablePfn;
        if (pgdPwc.lookup(vaddr, pudTablePfn)) {
            pgdCacheHits++;
            ADDRINT paddr = completePgdCacheHit(vaddr, pudTablePfn);

            // Update both TLBs with the translation
            pfn = paddr >> PAGE_SHIFT;
            l1Tlb.insert(vpn, pfn);
            l2Tlb.insert(vpn, pfn);
            return paddr;
        }

        // 6. Full page table walk needed
        fullWalks++;
        ADDRINT paddr = completeFullWalk(vaddr);

        // Update both TLBs with the translation
        pfn = paddr >> PAGE_SHIFT;
        l1Tlb.insert(vpn, pfn);
        l2Tlb.insert(vpn, pfn);
        return paddr;
    }

    // Print detailed page table and cache statistics
    void printDetailedStats(std::ostream& os) const {
        // Calculate totals for translation paths
        UINT64 totalTranslations = l1TlbHits + l2TlbHits + pmdCacheHits +
                                   pudCacheHits + pgdCacheHits + fullWalks;

        os << "\nTranslation Path Statistics:" << std::endl;
        os << "===========================" << std::endl;
        os << std::left << std::setw(30) << "Path" << std::right
           << std::setw(15) << "Count" << std::setw(15) << "Percentage"
           << std::endl;
        os << std::string(60, '-') << std::endl;

        os << std::left << std::setw(30) << "L1 TLB Hit" << std::right
           << std::setw(15) << l1TlbHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)l1TlbHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "L2 TLB Hit" << std::right
           << std::setw(15) << l2TlbHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)l2TlbHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "PMD PWC Hit" << std::right
           << std::setw(15) << pmdCacheHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)pmdCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "PUD PWC Hit" << std::right
           << std::setw(15) << pudCacheHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)pudCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "PGD PWC Hit" << std::right
           << std::setw(15) << pgdCacheHits << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)pgdCacheHits / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "Full Page Walk" << std::right
           << std::setw(15) << fullWalks << std::setw(15) << std::fixed
           << std::setprecision(2)
           << (totalTranslations > 0
                   ? (double)fullWalks / totalTranslations * 100.0
                   : 0.0)
           << "%" << std::endl;

        os << std::left << std::setw(30) << "Total Translations" << std::right
           << std::setw(15) << totalTranslations << std::setw(15) << "100.00%"
           << std::endl;

        // Calculate TLB efficiency
        double tlbEfficiency =
            (double)(l1TlbHits + l2TlbHits) / totalTranslations * 100.0;
        os << "\nTLB Efficiency: " << std::fixed << std::setprecision(2)
           << tlbEfficiency << "% (translations resolved by L1 or L2 TLB)"
           << std::endl;

        // Cache statistics
        os << "\nCache Statistics:" << std::endl;
        os << "================" << std::endl;
        os << std::left << std::setw(30) << "Cache" << std::setw(10)
           << "Entries" << std::setw(10) << "Sets" << std::setw(10) << "Ways"
           << std::right << std::setw(15) << "Accesses" << std::setw(15)
           << "Hits" << std::setw(15) << "Hit Rate" << std::endl;
        os << std::string(105, '-') << std::endl;

        // TLB stats
        os << std::left << std::setw(30) << l1Tlb.getName() << std::setw(10)
           << l1Tlb.getSize() << std::setw(10) << l1Tlb.getNumSets()
           << std::setw(10) << l1Tlb.getNumWays() << std::right << std::setw(15)
           << l1Tlb.getAccesses() << std::setw(15) << l1Tlb.getHits()
           << std::setw(15) << std::fixed << std::setprecision(2)
           << l1Tlb.getHitRate() * 100.0 << "%" << std::endl;

        os << std::left << std::setw(30) << l2Tlb.getName() << std::setw(10)
           << l2Tlb.getSize() << std::setw(10) << l2Tlb.getNumSets()
           << std::setw(10) << l2Tlb.getNumWays() << std::right << std::setw(15)
           << l2Tlb.getAccesses() << std::setw(15) << l2Tlb.getHits()
           << std::setw(15) << std::fixed << std::setprecision(2)
           << l2Tlb.getHitRate() * 100.0 << "%" << std::endl;

        // PWC stats
        os << std::left << std::setw(30) << pgdPwc.getName() << std::setw(10)
           << pgdPwc.getSize() << std::setw(10) << pgdPwc.getNumSets()
           << std::setw(10) << pgdPwc.getNumWays() << std::right
           << std::setw(15) << pgdPwc.getAccesses() << std::setw(15)
           << pgdPwc.getHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << pgdPwc.getHitRate() * 100.0 << "%"
           << std::endl;

        os << std::left << std::setw(30) << pudPwc.getName() << std::setw(10)
           << pudPwc.getSize() << std::setw(10) << pudPwc.getNumSets()
           << std::setw(10) << pudPwc.getNumWays() << std::right
           << std::setw(15) << pudPwc.getAccesses() << std::setw(15)
           << pudPwc.getHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << pudPwc.getHitRate() * 100.0 << "%"
           << std::endl;

        os << std::left << std::setw(30) << pmdPwc.getName() << std::setw(10)
           << pmdPwc.getSize() << std::setw(10) << pmdPwc.getNumSets()
           << std::setw(10) << pmdPwc.getNumWays() << std::right
           << std::setw(15) << pmdPwc.getAccesses() << std::setw(15)
           << pmdPwc.getHits() << std::setw(15) << std::fixed
           << std::setprecision(2) << pmdPwc.getHitRate() * 100.0 << "%"
           << std::endl;

        os << "\nVirtual Address Bit Ranges Used for PWC Tags:" << std::endl;
        os << std::left << std::setw(30) << pgdPwc.getName() << "["
           << pgdPwc.getHighBit() << ":" << pgdPwc.getLowBit() << "]"
           << std::endl;
        os << std::left << std::setw(30) << pudPwc.getName() << "["
           << pudPwc.getHighBit() << ":" << pudPwc.getLowBit() << "]"
           << std::endl;
        os << std::left << std::setw(30) << pmdPwc.getName() << "["
           << pmdPwc.getHighBit() << ":" << pmdPwc.getLowBit() << "]"
           << std::endl;

        // Page table statistics by level
        os << "\nPage Table Statistics by Level:" << std::endl;
        os << "==============================" << std::endl;

        // Header
        os << std::setw(30) << std::left << "Level" << std::setw(15)
           << std::right << "Accesses" << std::setw(15) << std::right
           << "Tables" << std::setw(15) << std::right << "Entries"
           << std::setw(15) << std::right << "Avg Fill %" << std::endl;
        os << std::string(90, '-') << std::endl;

        // Calculate and print stats for each level
        printLevelStats(os, pgdStats);
        printLevelStats(os, pudStats);
        printLevelStats(os, pmdStats);
        printLevelStats(os, pteStats);

        os << "\nTotal page tables: " << pageTables.size() << std::endl;
        os << "Total memory for page tables: "
           << (pageTables.size() * MEMTRACE_PAGE_SIZE) / (1024.0 * 1024.0)
           << " MB" << std::endl;
    }

    void printMemoryStats(std::ostream& os) const {
        os << "\nCache Access Statistics (from Page Table):\n";
        os << "=========================================\n";
        os << std::left << std::setw(35) << "Page Table Entry data Cache Hits"
           << std::right << std::setw(10) << pteDataCacheHits << "\n";
        os << std::left << std::setw(35) << "Page Table Entry data Cache Misses"
           << std::right << std::setw(10) << pteDataCacheMisses << "\n";
        os << std::left << std::setw(35) << "Page Walk Memory Accesses"
           << std::right << std::setw(10) << pageWalkMemAccess << "\n";
        os << std::left << std::setw(35) << "Page Table Entry Cache hits ratio"
           << std::right << std::setw(10)
           << (pteDataCacheHits + pteDataCacheMisses > 0
                   ? (double)pteDataCacheHits /
                         (pteDataCacheHits + pteDataCacheMisses) * 100.0
                   : 0.0)
           << "%\n";
    }

   private:
    // Helper to print stats for a page table level
    void printLevelStats(std::ostream& os,
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
           << avgFill << std::endl;
    }

   public:
    // Get statistics
    size_t getNumPageTables() const { return pageTables.size(); }

    // TLB statistics
    double getL1TlbHitRate() const { return l1Tlb.getHitRate(); }
    double getL2TlbHitRate() const { return l2Tlb.getHitRate(); }
    size_t getL1TlbAccesses() const { return l1Tlb.getAccesses(); }
    size_t getL2TlbAccesses() const { return l2Tlb.getAccesses(); }
    size_t getL1TlbHits() const { return l1Tlb.getHits(); }
    size_t getL2TlbHits() const { return l2Tlb.getHits(); }

    // Overall TLB efficiency
    double getTlbEfficiency() const {
        UINT64 totalTranslations = l1TlbHits + l2TlbHits + pmdCacheHits +
                                   pudCacheHits + pgdCacheHits + fullWalks;
        return totalTranslations > 0
                   ? (double)(l1TlbHits + l2TlbHits) / totalTranslations
                   : 0.0;
    }

    // Page walk statistics
    size_t getPageTableWalks() const { return fullWalks; }
    size_t getFullWalks() const { return fullWalks; }
    size_t getPgdCacheHits() const { return pgdCacheHits; }
    size_t getPudCacheHits() const { return pudCacheHits; }
    size_t getPmdCacheHits() const { return pmdCacheHits; }
    double getPgdCacheHitRate() const { return pgdPwc.getHitRate(); }
    double getPudCacheHitRate() const { return pudPwc.getHitRate(); }
    double getPmdCacheHitRate() const { return pmdPwc.getHitRate(); }
};