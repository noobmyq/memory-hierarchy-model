// data_cache.h
#pragma once

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>
#include "cache.h"
#include "common.h"

class DataCache : public SetAssociativeCache<UINT64, UINT64> {
   private:
    UINT32 lineSize;
    UINT32 offsetBits;
    UINT64 readAccesses;
    UINT64 readHits;
    UINT64 writeAccesses;
    UINT64 writeHits;
    UINT64 writebacks;
    UINT64 coldMisses;
    UINT64 capacityMisses;
    UINT64 conflictMisses;
    DataCache*
        nextLevel;  // pointer to next level cache (L2 or L3), or nullptr if last level
    UINT64*
        memAccessCounter;  // pointer to memory access counter (for last level)

   protected:
    UINT64 getSetIndex(const UINT64& tag) const override {
        UINT64 index = tag & (numSets - 1);
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
                UINT64 nextLevelTag =
                    tag << offsetBits >> nextLevel->getOffsetBits();
                // we don't want to lookup here, it doesn't matter and it is not in critical path
                nextLevel->insert(tag, parse_value, /*isWrite*/ true);
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
    DataCache(const std::string& name, UINT64 totalSize, UINT64 associativity,
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
    void setMemCounter(UINT64* memCountPt) { memAccessCounter = memCountPt; }
    UINT32 getOffsetBits() const { return offsetBits; }
    UINT64 getWritebacks() const { return writebacks; }

    bool lookup(const uint64_t& tag, uint64_t& value, bool isWrite = false) {
        bool hit = SetAssociativeCache::lookup(tag, value);
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

        if (!hit) {
            if (globalLruCounter < numSets * numWays)
                coldMisses++;
            else if (findLruWay(getSetIndex(tag)))
                capacityMisses++;
            else
                conflictMisses++;
        }
        return hit;
    }

    // New statistics methods
    uint64_t getAllMisses() const {
        return coldMisses + capacityMisses + conflictMisses;
    }
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

   public:
    UINT64 memAccessCount;

   public:
    CacheHierarchy(UINT64 l1Size, UINT64 l1Ways, UINT64 l1Line, UINT64 l2Size,
                   UINT64 l2Ways, UINT64 l2Line, UINT64 l3Size, UINT64 l3Ways,
                   UINT64 l3Line)
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

    // translation access start from L2, do not access L1
    bool translate_lookup(ADDRINT paddr, UINT64& value) {
        UINT64 l2CacheTag = paddr >> l2Cache.getOffsetBits();
        // L2 access
        if (l2Cache.lookup(l2CacheTag, value)) {
            return true;
        }
        UINT64 l3CacheTag = paddr >> l3Cache.getOffsetBits();
        // L3 access (on L1 & L2 miss)
        if (l3Cache.lookup(l3CacheTag, value)) {
            // On L3 hit, fill L2
            l2Cache.insert(l2CacheTag, value, false);
            return true;
        }

        // Miss in all caches: access main memory
        memAccessCount++;  // memory read for the new block
        // Fill all levels with the new block (inclusive cache policy)
        l3Cache.insert(l3CacheTag, value, false);
        l2Cache.insert(l2CacheTag, value, false);
        return false;
    }

    bool access(ADDRINT paddr, UINT64& value, bool isWrite) {
        UINT64 l1CacheTag = paddr >> l1Cache.getOffsetBits();
        // L1 access
        if (l1Cache.lookup(l1CacheTag, value, isWrite)) {
            // Hit in L1. If isWrite, L1 line is now marked dirty (no write-through).
            if (isWrite)
                l1Cache.insert(l1CacheTag, value,
                               isWrite);  // update value (dirty flag set)
            return true;
        }

        UINT64 l2CacheTag = paddr >> l2Cache.getOffsetBits();
        // L2 access (on L1 miss)
        if (l2Cache.lookup(l2CacheTag, value, isWrite)) {
            // On L2 hit, fill L1 with the block
            l1Cache.insert(l1CacheTag, value, isWrite);
            // (If write, L1 will be dirty; no immediate write to L3)
            if (isWrite) {
                l2Cache.insert(l1CacheTag, value,
                               isWrite);  // update value (dirty flag set)
            }
            return true;
        }
        UINT64 l3CacheTag = paddr >> l3Cache.getOffsetBits();
        // L3 access (on L1 & L2 miss)
        if (l3Cache.lookup(l3CacheTag, value, isWrite)) {
            // On L3 hit, fill L2 and L1
            if (isWrite) {
                l3Cache.insert(l1CacheTag, value,
                               isWrite);  // update value (dirty flag set)
            }
            l2Cache.insert(l2CacheTag, value, false);
            l1Cache.insert(l1CacheTag, value, isWrite);
            // L1 marked dirty if write; L2 remains clean copy
            return true;
        }

        // Miss in all caches: access main memory
        memAccessCount++;  // memory read for the new block
        assert(l3Cache.getAccesses() - l3Cache.getHits() +
                   l3Cache.getWritebacks() ==
               memAccessCount);
        // Fill all levels with the new block (inclusive cache policy)
        l3Cache.insert(l3CacheTag, value, false);
        l2Cache.insert(l2CacheTag, value, false);
        l1Cache.insert(l1CacheTag, value, isWrite);
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