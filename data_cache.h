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
    DataCache*
        nextLevel;  // pointer to next level cache (L2 or L3), or nullptr if last level
    size_t*
        memAccessCounter;  // pointer to memory access counter (for last level)

   protected:
    size_t getSetIndex(const UINT64& paddr) const override {
        UINT64 index = (paddr >> offsetBits) & (numSets - 1);
        return index;
    }
    // Override eviction handler to propagate write-back one level down
    void handleEviction(const UINT64& tag, const UINT64& value,
                        bool dirty) override {
        if (dirty) {
            writebacks++;  // count this write-back in this cache's stats
            if (nextLevel) {
                // Write the evicted block to the next cache level (write-back)
                UINT64 parse_value = value;
                nextLevel->access(tag, parse_value, /*isWrite*/ true);
            } else {
                // No next level (this is L3) – write back to main memory
                if (memAccessCounter) {
                    ++(*memAccessCounter);  // count a memory write access
                }
            }
        }
        // If not dirty, no action needed (clean eviction)
    }

   public:
    DataCache(const std::string& name, size_t totalSize, size_t associativity,
              UINT32 lineSize)
        : SetAssociativeCache<UINT64, UINT64>(
              name, totalSize / (associativity * lineSize), associativity),
          lineSize(lineSize) {
        offsetBits = static_log2(lineSize);
        readAccesses = readHits = 0;
        writeAccesses = writeHits = 0;
        writebacks = coldMisses = capacityMisses = conflictMisses = 0;
    }
    // Set up links to next level and memory counter for write-back propagation
    void setNextLevel(DataCache* nxt) { nextLevel = nxt; }
    void setMemCounter(size_t* memCountPt) { memAccessCounter = memCountPt; }

    bool access(ADDRINT paddr, UINT64& value, bool isWrite) {
        bool hit = lookup(paddr, value);

        // Update detailed R/W statistics
        if (isWrite) {
            writeAccesses++;
            if (hit) {
                writeHits++;
            }
        } else {
            readAccesses++;
            if (hit) {
                readHits++;
            }
        }

        // If it’s a write hit, mark the cache line as dirty
        if (hit && isWrite) {
            insert(paddr, value, true);  // update existing line, mark dirty
        }

        // Analyze miss type if not hit
        if (!hit) {
            if (globalLruCounter < numSets * numWays)
                coldMisses++;
            else if (findLruWay(getSetIndex(paddr)))
                capacityMisses++;
            else
                conflictMisses++;

            // On a miss, bring the block into this cache
            insert(paddr, value,
                   isWrite);  // write-allocate if write (dirty), normal if read
        }

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
        // In DataCache::printDetailedStats (adding writeback count):
        os << std::left << std::setw(25) << "Writebacks" << std::right
           << std::setw(15) << writebacks << "\n";
    }
};

class CacheHierarchy {
   private:
    DataCache l1Cache;
    DataCache l2Cache;
    DataCache l3Cache;
    size_t memAccessCount;

   public:
    CacheHierarchy(size_t l1Size, size_t l1Ways, size_t l1Line, size_t l2Size,
                   size_t l2Ways, size_t l2Line, size_t l3Size, size_t l3Ways,
                   size_t l3Line)
        : l1Cache("L1 Cache", l1Size, l1Ways, l1Line),
          l2Cache("L2 Cache", l2Size, l2Ways, l2Line),
          l3Cache("L3 Cache", l3Size, l3Ways, l3Line),
          memAccessCount(0) {
        // Set up cache hierarchy
        l1Cache.setNextLevel(&l2Cache);
        l2Cache.setNextLevel(&l3Cache);
        l3Cache.setNextLevel(nullptr);  // L3 has no next level
        l1Cache.setMemCounter(&memAccessCount);
        l2Cache.setMemCounter(&memAccessCount);
        l3Cache.setMemCounter(&memAccessCount);  // L3 writes to memory
    }

    bool lookup(ADDRINT paddr, UINT64& value) {
        // L1 access
        if (l1Cache.lookup(paddr, value))
            return true;

        // L2 access
        if (l2Cache.lookup(paddr, value)) {
            l1Cache.insert(paddr, value);  // Fill L1
            return true;
        }

        // L3 access
        if (l3Cache.lookup(paddr, value)) {
            l1Cache.insert(paddr, value);  // Fill L1
            l2Cache.insert(paddr, value);  // Fill L2
            return true;
        }

        return false;  // Miss in all caches
    }

    bool access(ADDRINT paddr, UINT64& value, bool isWrite) {
        // L1 access
        if (l1Cache.access(paddr, value, isWrite)) {
            // Hit in L1. If isWrite, L1 line is now marked dirty (no write-through).
            return true;
        }

        // L2 access (on L1 miss)
        if (l2Cache.access(paddr, value, isWrite)) {
            // On L2 hit, fill L1 with the block
            l1Cache.insert(paddr, value, isWrite);
            // (If write, L1 will be dirty; no immediate write to L3)
            return true;
        }

        // L3 access (on L1 & L2 miss)
        if (l3Cache.access(paddr, value, isWrite)) {
            // On L3 hit, fill L2 and L1
            l2Cache.insert(paddr, value, false);
            l1Cache.insert(paddr, value, isWrite);
            // L1 marked dirty if write; L2 remains clean copy
            return true;
        }

        // Miss in all caches: access main memory
        memAccessCount++;  // memory read for the new block
        // Fill all levels with the new block (inclusive cache policy)
        l3Cache.insert(paddr, value, false);
        l2Cache.insert(paddr, value, false);
        l1Cache.insert(paddr, value, isWrite);
        // If isWrite, L1 is dirty; L2 and L3 have clean copies.
        return false;
    }

    void printStats(std::ostream& os) const {
        os << "\n=== Cache Hierarchy Statistics ===\n";
        printCacheStats(os, l1Cache);
        printCacheStats(os, l2Cache);
        printCacheStats(os, l3Cache);
        os << "Memory Accesses: " << memAccessCount << "\n";
        os << "Total Access Cost (cycles): "
           << l1Cache.getAccesses() * 1 +       // L1 access cycles
                  l2Cache.getAccesses() * 4 +   // L2 access cycles
                  l3Cache.getAccesses() * 10 +  // L3 access cycles
                  memAccessCount * 100          // Memory access cycles
           << "\n";
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