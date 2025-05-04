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
    UINT64 lineSize_;
    UINT64 offsetBits_;
    UINT64 readAccesses_;
    UINT64 readHits_;
    UINT64 writeAccesses_;
    UINT64 writeHits_;
    UINT64 writebacks_;
    UINT64 coldMisses_;
    UINT64 capacityMisses_;
    UINT64 conflictMisses_;
    DataCache*
        nextLevel_;  // pointer to next level cache (L2 or L3), or nullptr if last level
    UINT64*
        memAccessCounter_;  // pointer to memory access counter (for last level)

   protected:
    UINT64 GetSetIndex(const UINT64& tag) const override {
        UINT64 index = tag & (numSets_ - 1);
        return index;
    }
    // Override eviction handler to propagate write-back one level down
    void HandleEviction(const UINT64& tag, const UINT64& value,
                        bool dirty) override {
        if (dirty) {
            writebacks_++;  // count this write-back in this cache's stats
            if (nextLevel_) {
                // Write the evicted block to the next cache level (write-back)
                UINT64 parseValue = value;
                UINT64 nextLevelTag =
                    tag << offsetBits_ >> nextLevel_->GetOffsetBits();
                // we don't want to Lookup here, it doesn't matter and it is not in critical path
                nextLevel_->Insert(tag, parseValue, /*isWrite*/ true);
            } else {
                // No next level (this is L3) – write back to main memory
                if (memAccessCounter_) {
                    ++(*memAccessCounter_);  // count a memory write access
                }
            }
        }
        // If not dirty, no action needed (clean eviction)
    }

   public:
    DataCache(const std::string& name, UINT64 totalSize, UINT64 associativity,
              UINT64 lineSize)
        : SetAssociativeCache<UINT64, UINT64>(
              name, totalSize / (associativity * lineSize), associativity),
          lineSize_(lineSize) {
        offsetBits_ = StaticLog2(lineSize);
        readAccesses_ = readHits_ = 0;
        writeAccesses_ = writeHits_ = 0;
        writebacks_ = coldMisses_ = capacityMisses_ = conflictMisses_ = 0;
    }
    // Set up links to next level and memory counter for write-back propagation
    void SetNextLevel(DataCache* nxt) { nextLevel_ = nxt; }
    void SetMemCounter(UINT64* memCountPt) { memAccessCounter_ = memCountPt; }
    UINT64 GetOffsetBits() const { return offsetBits_; }
    UINT64 GetWritebacks() const { return writebacks_; }

    bool Lookup(const uint64_t& tag, uint64_t& value, bool isWrite = false) {
        bool hit = SetAssociativeCache::Lookup(tag, value);
        if (isWrite) {
            writeAccesses_++;
            if (hit) {
                writeHits_++;
            }
        } else {
            readAccesses_++;
            if (hit) {
                readHits_++;
            }
        }

        if (!hit) {
            if (globalLruCounter_ < numSets_ * numWays_)
                coldMisses_++;
            else if (FindLruWay(GetSetIndex(tag)))
                capacityMisses_++;
            else
                conflictMisses_++;
        }
        return hit;
    }

    // New statistics methods
    uint64_t GetAllMisses() const {
        return coldMisses_ + capacityMisses_ + conflictMisses_;
    }
    double GetReadHitRate() const {
        return readAccesses_ ? (double)readHits_ / readAccesses_ : 0;
    }
    double GetWriteHitRate() const {
        return writeAccesses_ ? (double)writeHits_ / writeAccesses_ : 0;
    }
    void PrintDetailedStats(std::ostream& os) const {
        os << "\nData Cache Detailed Statistics:\n";
        os << "==============================\n";
        os << std::left << std::setw(25) << "Total Accesses" << std::right
           << std::setw(15) << GetAccesses() << "\n";
        os << std::left << std::setw(25) << "Read Accesses" << std::right
           << std::setw(15) << readAccesses_ << "\n";
        os << std::left << std::setw(25) << "Read Hit Rate" << std::setw(15)
           << std::fixed << std::setprecision(2) << GetReadHitRate() * 100
           << "%\n";
        os << std::left << std::setw(25) << "Write Accesses" << std::right
           << std::setw(15) << writeAccesses_ << "\n";
        os << std::left << std::setw(25) << "Write Hit Rate" << std::setw(15)
           << std::fixed << std::setprecision(2) << GetWriteHitRate() * 100
           << "%\n";
        os << std::left << std::setw(25) << "Cold Misses" << std::right
           << std::setw(15) << coldMisses_ << "\n";
        os << std::left << std::setw(25) << "Capacity Misses" << std::right
           << std::setw(15) << capacityMisses_ << "\n";
        os << std::left << std::setw(25) << "Conflict Misses" << std::right
           << std::setw(15) << conflictMisses_ << "\n";
        // In DataCache::printDetailedStats (adding writeback count):
        os << std::left << std::setw(25) << "Writebacks" << std::right
           << std::setw(15) << writebacks_ << "\n";
    }
};

class CacheHierarchy {
   private:
    DataCache l1Cache_;
    DataCache l2Cache_;
    DataCache l3Cache_;

   public:
    UINT64 memAccessCount;

   public:
    CacheHierarchy(UINT64 l1Size, UINT64 l1Ways, UINT64 l1Line, UINT64 l2Size,
                   UINT64 l2Ways, UINT64 l2Line, UINT64 l3Size, UINT64 l3Ways,
                   UINT64 l3Line)
        : l1Cache_("L1 Cache", l1Size, l1Ways, l1Line),
          l2Cache_("L2 Cache", l2Size, l2Ways, l2Line),
          l3Cache_("L3 Cache", l3Size, l3Ways, l3Line),
          memAccessCount(0) {
        // Set up cache hierarchy
        l1Cache_.SetNextLevel(&l2Cache_);
        l2Cache_.SetNextLevel(&l3Cache_);
        l3Cache_.SetNextLevel(nullptr);  // L3 has no next level
        l1Cache_.SetMemCounter(&memAccessCount);
        l2Cache_.SetMemCounter(&memAccessCount);
        l3Cache_.SetMemCounter(&memAccessCount);  // L3 writes to memory
    }

    // translation access start from L2, do not access L1
    bool TranslateLookup(ADDRINT paddr, UINT64& value,
                         TranslationStats& translationStats) {
        UINT64 l2CacheTag = paddr >> l2Cache_.GetOffsetBits();
        // L2 access
        translationStats.l2DataCacheAccess++;
        if (l2Cache_.Lookup(l2CacheTag, value)) {
            translationStats.l2DataCacheHits++;
            return true;
        }
        UINT64 l3CacheTag = paddr >> l3Cache_.GetOffsetBits();
        // L3 access (on L1 & L2 miss)
        translationStats.l3DataCacheAccess++;
        if (l3Cache_.Lookup(l3CacheTag, value)) {
            // On L3 hit, fill L2
            translationStats.l3DataCacheHits++;
            l2Cache_.Insert(l2CacheTag, value, false);
            return true;
        }

        // Miss in all caches: access main memory
        memAccessCount++;  // memory read for the new block
        // Fill all levels with the new block (inclusive cache policy)
        l3Cache_.Insert(l3CacheTag, value, false);
        l2Cache_.Insert(l2CacheTag, value, false);
        return false;
    }

    bool Access(ADDRINT paddr, UINT64& value, bool isWrite) {
        UINT64 l1CacheTag = paddr >> l1Cache_.GetOffsetBits();
        // L1 access
        if (l1Cache_.Lookup(l1CacheTag, value, isWrite)) {
            // Hit in L1. If isWrite, L1 line is now marked dirty (no write-through).
            if (isWrite)
                l1Cache_.Insert(l1CacheTag, value,
                                isWrite);  // update value (dirty flag set)
            return true;
        }

        UINT64 l2CacheTag = paddr >> l2Cache_.GetOffsetBits();
        // L2 access (on L1 miss)
        if (l2Cache_.Lookup(l2CacheTag, value, isWrite)) {
            // On L2 hit, fill L1 with the block
            l1Cache_.Insert(l1CacheTag, value, isWrite);
            // (If write, L1 will be dirty; no immediate write to L3)
            if (isWrite) {
                l2Cache_.Insert(l1CacheTag, value,
                                isWrite);  // update value (dirty flag set)
            }
            return true;
        }
        UINT64 l3CacheTag = paddr >> l3Cache_.GetOffsetBits();
        // L3 access (on L1 & L2 miss)
        if (l3Cache_.Lookup(l3CacheTag, value, isWrite)) {
            // On L3 hit, fill L2 and L1
            if (isWrite) {
                l3Cache_.Insert(l1CacheTag, value,
                                isWrite);  // update value (dirty flag set)
            }
            l2Cache_.Insert(l2CacheTag, value, false);
            l1Cache_.Insert(l1CacheTag, value, isWrite);
            // L1 marked dirty if write; L2 remains clean copy
            return true;
        }

        // Miss in all caches: access main memory
        memAccessCount++;  // memory read for the new block
        assert(l3Cache_.GetAccesses() - l3Cache_.GetHits() +
                   l3Cache_.GetWritebacks() ==
               memAccessCount);
        // Fill all levels with the new block (inclusive cache policy)
        l3Cache_.Insert(l3CacheTag, value, false);
        l2Cache_.Insert(l2CacheTag, value, false);
        l1Cache_.Insert(l1CacheTag, value, isWrite);
        // If isWrite, L1 is dirty; L2 and L3 have clean copies.
        return false;
    }

    void PrintStats(std::ostream& os) const {
        os << "\n=== Cache Hierarchy Statistics ===\n";
        PrintCacheStats(os, l1Cache_);
        PrintCacheStats(os, l2Cache_);
        PrintCacheStats(os, l3Cache_);
        os << "Memory Accesses: " << memAccessCount << "\n";
        os << "Total Access Cost (cycles): "
           << l1Cache_.GetAccesses() * 1 +       // L1 access cycles
                  l2Cache_.GetAccesses() * 4 +   // L2 access cycles
                  l3Cache_.GetAccesses() * 10 +  // L3 access cycles
                  memAccessCount * 100           // Memory access cycles
           << "\n";
    }

   private:
    void PrintCacheStats(std::ostream& os, const DataCache& cache) const {
        os << "[" << cache.GetName() << "]\n"
           << "Size: " << cache.GetSize() / 1024 << "KB\n"
           << "Ways: " << cache.GetNumWays() << "\n"
           << "Hit Rate: " << std::fixed << std::setprecision(2)
           << cache.GetHitRate() * 100 << "%\n"
           << "Accesses: " << cache.GetAccesses() << "\n"
           << "Misses: " << cache.GetAccesses() - cache.GetHits() << "\n";
        cache.PrintDetailedStats(os);
        os << "---------------------------------\n";
        os << std::endl;
    }
};