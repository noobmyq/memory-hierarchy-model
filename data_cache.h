// data_cache.h
#pragma once

#include <iomanip>
#include <iostream>
#include "cache.h"
#include "common.h"

class DataCache : public SetAssociativeCache<UINT64, UINT64> {
   private:
    UINT32 lineSize;
    UINT32 offsetBits;
    size_t readAccesses;
    size_t readHits;
    size_t writeAccesses;
    size_t writeHits;
    size_t writebacks;
    size_t coldMisses;
    size_t capacityMisses;
    size_t conflictMisses;

    static UINT32 log2(UINT32 n) {
        if (n == 0)
            return 0;
        UINT32 result = 0;
        while (n >>= 1)
            result++;
        return result;
    }

   protected:
    size_t getSetIndex(const UINT64& paddr) const override {
        UINT64 index = (paddr >> offsetBits) & (numSets - 1);
        return index;
    }

   public:
    DataCache(const std::string& name, size_t totalSize, size_t associativity,
              UINT32 lineSize)
        : SetAssociativeCache<UINT64, UINT64>(
              name, totalSize / (associativity * lineSize), associativity),
          lineSize(lineSize) {
        offsetBits = log2(lineSize);
        readAccesses = readHits = 0;
        writeAccesses = writeHits = 0;
        writebacks = coldMisses = capacityMisses = conflictMisses = 0;
    }

    bool access(ADDRINT paddr, UINT64& value, bool isWrite) {
        bool hit = lookup(paddr, value);

        // Update detailed statistics
        if (isWrite) {
            writeAccesses++;
            if (hit)
                writeHits++;
        } else {
            readAccesses++;
            if (hit)
                readHits++;
        }

        // Analyze miss types
        if (!hit) {
            if (globalLruCounter < numSets * numWays)
                coldMisses++;
            else if (findLruWay(getSetIndex(paddr)))
                capacityMisses++;
            else
                conflictMisses++;
        }

        // Insert new entry
        if (!hit)
            insert(paddr, value);
        return hit;
    }

    // New statistics methods
    double getReadHitRate() const {
        return readAccesses ? (double)readHits / readAccesses : 0;
    }
    double getWriteHitRate() const {
        return writeAccesses ? (double)writeHits / writeAccesses : 0;
    }
    void printDetailedStats(std::ostream& os) const {
        os << "\nData Cache Detailed Statistics:\n";
        os << "==============================\n";
        os << std::left << std::setw(25) << "Total Accesses" << std::right
           << std::setw(15) << getAccesses() << "\n";
        os << std::left << std::setw(25) << "Read Accesses" << std::right
           << std::setw(15) << readAccesses << "\n";
        os << std::left << std::setw(25) << "Read Hit Rate" << std::setw(15)
           << std::fixed << std::setprecision(2) << getReadHitRate() * 100
           << "%\n";
        os << std::left << std::setw(25) << "Write Accesses" << std::right
           << std::setw(15) << writeAccesses << "\n";
        os << std::left << std::setw(25) << "Write Hit Rate" << std::setw(15)
           << std::fixed << std::setprecision(2) << getWriteHitRate() * 100
           << "%\n";
        os << std::left << std::setw(25) << "Cold Misses" << std::right
           << std::setw(15) << coldMisses << "\n";
        os << std::left << std::setw(25) << "Capacity Misses" << std::right
           << std::setw(15) << capacityMisses << "\n";
        os << std::left << std::setw(25) << "Conflict Misses" << std::right
           << std::setw(15) << conflictMisses << "\n";
        os << std::left << std::setw(25) << "Writebacks" << std::right
           << std::setw(15) << writebacks << "\n";
    }
};

class ModifiedCacheHierarchy {
   private:
    DataCache l1iCache;  // L1 Instruction Cache
    DataCache l1dCache;  // L1 Data Cache
    DataCache l2Cache;   // Shared L2 Cache
    DataCache l3Cache;   // Shared L3 Cache
    size_t memAccessCount;
    bool separateL1I;  // Flag to indicate if we're using separate I-cache

   public:
    ModifiedCacheHierarchy(size_t l1Size, size_t l1Ways, size_t l1Line,
                           size_t l2Size, size_t l2Ways, size_t l2Line,
                           size_t l3Size, size_t l3Ways, size_t l3Line,
                           bool useSeparateL1I = true)
        : l1iCache("L1 Instruction Cache", l1Size, l1Ways, l1Line),
          l1dCache("L1 Data Cache", l1Size, l1Ways, l1Line),
          l2Cache("L2 Cache (Unified)", l2Size, l2Ways, l2Line),
          l3Cache("L3 Cache (Unified)", l3Size, l3Ways, l3Line),
          memAccessCount(0),
          separateL1I(useSeparateL1I) {}

    // Instruction cache access
    bool instructionAccess(ADDRINT paddr, UINT64& value) {
        if (!separateL1I) {
            // If not using separate I-cache, use data cache hierarchy
            return access(paddr, value, true);  // Instruction reads
        }

        // L1I access
        if (l1iCache.access(paddr, value,
                            true))  // Instructions are always reads
            return true;

        // L1I miss, try L2 (shared)
        if (l2Cache.access(paddr, value, true)) {
            l1iCache.insert(paddr, value);  // Fill L1I cache
            return true;
        }

        // L2 miss, try L3 (shared)
        if (l3Cache.access(paddr, value, true)) {
            l1iCache.insert(paddr, value);  // Fill L1I cache
            l2Cache.insert(paddr, value);   // Fill L2 cache
            return true;
        }

        // All caches missed
        memAccessCount++;
        l1iCache.insert(paddr, value);  // Fill L1I cache
        l2Cache.insert(paddr, value);   // Fill L2 cache
        l3Cache.insert(paddr, value);   // Fill L3 cache
        return false;
    }

    // Data cache access (original cache access logic)
    bool access(ADDRINT paddr, UINT64& value, bool isRead) {
        // L1D access
        if (l1dCache.access(paddr, value, isRead)) {
            if (isRead) {
                // Read through
                l2Cache.access(paddr, value, isRead);
                l3Cache.access(paddr, value, isRead);
            }
            return true;
        }

        // L2 access (shared between L1I and L1D)
        if (l2Cache.access(paddr, value, isRead)) {
            l1dCache.insert(paddr, value);  // Fill L1D cache
            if (!isRead) {
                // Write through
                l3Cache.access(paddr, value, isRead);
            }
            return true;
        }

        // L3 access (shared)
        if (l3Cache.access(paddr, value, isRead)) {
            l1dCache.insert(paddr, value);  // Fill L1D cache
            l2Cache.insert(paddr, value);   // Fill L2 cache
            return true;
        }

        // All caches missed
        memAccessCount++;
        l1dCache.insert(paddr, value);  // Fill L1D cache
        l2Cache.insert(paddr, value);   // Fill L2 cache
        l3Cache.insert(paddr, value);   // Fill L3 cache
        return false;
    }

    // Lookup function
    bool lookup(ADDRINT paddr, UINT64& value) {
        // Try L1 data cache
        if (l1dCache.lookup(paddr, value))
            return true;

        // L2 lookup (shared)
        if (l2Cache.lookup(paddr, value)) {
            l1dCache.insert(paddr, value);  // Fill L1D
            return true;
        }

        // L3 lookup (shared)
        if (l3Cache.lookup(paddr, value)) {
            l1dCache.insert(paddr, value);  // Fill L1D
            l2Cache.insert(paddr, value);   // Fill L2
            return true;
        }

        return false;  // All caches missed
    }

    void printStats(std::ostream& os) const {
        os << "\n=== Cache Hierarchy Statistics ===\n";

        // Print instruction cache stats if separate
        if (separateL1I) {
            printCacheStats(os, l1iCache);
        }

        // Print data and unified cache stats
        printCacheStats(os, l1dCache);
        printCacheStats(os, l2Cache);
        printCacheStats(os, l3Cache);

        os << "Memory Accesses: " << memAccessCount << "\n";

        // Calculate total access cost
        UINT64 totalCost = 0;

        if (separateL1I) {
            totalCost += l1iCache.getAccesses() * 1;  // L1I access cycles
        }

        totalCost += l1dCache.getAccesses() * 1 +  // L1D access cycles
                     l2Cache.getAccesses() * 4 +   // L2 access cycles
                     l3Cache.getAccesses() * 10 +  // L3 access cycles
                     memAccessCount * 100;         // Memory access cycles

        os << "Total Access Cost (cycles): " << totalCost << "\n";
    }

   private:
    void printCacheStats(std::ostream& os, const DataCache& cache) const {
        os << "[" << cache.getName() << "]\n"
           << "Size: " << cache.getSize() / 1024 << "KB\n"
           << "Ways: " << cache.getNumWays() << "\n"
           << "Hit Rate: " << std::fixed << std::setprecision(2)
           << cache.getHitRate() * 100 << "%\n"
           << "Accesses: " << cache.getAccesses() << "\n"
           << "Misses: " << cache.getAccesses() - cache.getHits() << "\n";
        cache.printDetailedStats(os);
        os << "---------------------------------\n";
        os << std::endl;
    }
};