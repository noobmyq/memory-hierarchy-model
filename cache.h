#pragma once

#include <string>
#include <vector>
#include "common.h"

// Base class for set-associative caches
template <typename TagType, typename ValueType>
class SetAssociativeCache {
   protected:
    struct CacheEntry {
        TagType tag;
        ValueType value;
        bool valid;
        bool dirty;  // NEW: dirty bit for write-back policy
        UINT64 lruCounter;

        CacheEntry()
            : tag(0), value(0), valid(false), dirty(false), lruCounter(0) {}
    };

    std::string name_;         // Cache name_ for reporting
    UINT64 numSets_;           // Number of sets_ in the cache
    UINT64 numWays_;           // Number of ways per set (associativity)
    UINT64 accesses_;          // Access counter
    UINT64 hits_;              // Hit counter
    UINT64 globalLruCounter_;  // Global counter for LRU policy
    std::vector<std::vector<CacheEntry>> sets_;  // Cache storage [set][way]

    // Find the LRU entry in a set
    UINT64 FindLruWay(UINT64 setIndex) const {
        UINT64 lruWay = 0;
        UINT64 minCounter = sets_[setIndex][0].lruCounter;

        for (UINT64 way = 0; way < numWays_; way++) {
            if (!sets_[setIndex][way].valid) {
                return way;  // Return first invalid entry
            }
            if (sets_[setIndex][way].lruCounter < minCounter) {
                minCounter = sets_[setIndex][way].lruCounter;
                lruWay = way;
            }
        }
        return lruWay;
    }

    // Update LRU status for an entry
    void UpdateLru(UINT64 setIndex, UINT64 wayIndex) {
        sets_[setIndex][wayIndex].lruCounter = ++globalLruCounter_;
    }

    // Hash function to map from tag to set index
    virtual UINT64 GetSetIndex(const TagType& tag) const = 0;

   protected:
    virtual void HandleEviction(const TagType& tag, const ValueType& value,
                                bool dirty) = 0;  // Pure virtual function

   public:
    SetAssociativeCache(const std::string& cacheName, UINT64 numSets,
                        UINT64 ways)
        : name_(cacheName),
          numSets_(numSets),
          numWays_(ways),
          accesses_(0),
          hits_(0),
          globalLruCounter_(0) {

        // Initialize cache structure
        sets_.resize(numSets);
        for (UINT64 i = 0; i < numSets; i++) {
            sets_[i].resize(numWays_);
            for (UINT64 j = 0; j < numWays_; j++) {
                sets_[i][j].valid = false;
            }
        }
    }

    virtual ~SetAssociativeCache() = default;

    // Core lookup operation
    bool Lookup(const TagType& tag, ValueType& value) {
        accesses_++;
        UINT64 setIndex = GetSetIndex(tag);

        for (UINT64 way = 0; way < numWays_; way++) {
            if (sets_[setIndex][way].valid && sets_[setIndex][way].tag == tag) {
                hits_++;
                value = sets_[setIndex][way].value;
                UpdateLru(setIndex, way);
                return true;
            }
        }
        return false;
    }

    // In cache.h, inside SetAssociativeCache class:
    void Insert(const TagType& tag, const ValueType& value,
                bool isWrite = false) {
        UINT64 setIndex = GetSetIndex(tag);

        // If block already in cache, update value and mark dirty on write
        for (UINT64 way = 0; way < numWays_; ++way) {
            if (sets_[setIndex][way].valid && sets_[setIndex][way].tag == tag) {
                sets_[setIndex][way].value = value;
                if (isWrite) {
                    sets_[setIndex][way].dirty = true;  // mark as modified
                }
                // (If not a write, leave dirty flag as is)
                UpdateLru(setIndex, way);
                return;
            }
        }

        // Block not present – choose a victim to evict (LRU)
        UINT64 victimWay = FindLruWay(setIndex);
        bool evictValid = sets_[setIndex][victimWay].valid;
        bool evictDirty = false;
        TagType evictTag;
        ValueType evictValue;
        if (evictValid) {
            // Save evicted block info for write-back
            evictDirty = sets_[setIndex][victimWay].dirty;
            evictTag = sets_[setIndex][victimWay].tag;
            evictValue = sets_[setIndex][victimWay].value;
        }

        // Replace victim with new block
        sets_[setIndex][victimWay].tag = tag;
        sets_[setIndex][victimWay].value = value;
        sets_[setIndex][victimWay].valid = true;
        sets_[setIndex][victimWay].dirty =
            isWrite;  // dirty if this is a write access
        UpdateLru(setIndex, victimWay);

        // Write-back to next level if we evicted a dirty block
        if (evictValid && evictDirty) {
            HandleEviction(evictTag, evictValue, true);
        }
    }

    // Stats reporting methods
    UINT64 GetAccesses() const { return accesses_; }
    UINT64 GetHits() const { return hits_; }
    double GetHitRate() const {
        return accesses_ > 0 ? (double)hits_ / accesses_ : 0.0;
    }
    const std::string& GetName() const { return name_; }
    UINT64 GetSize() const { return numSets_ * numWays_; }
    UINT64 GetNumSets() const { return numSets_; }
    UINT64 GetNumWays() const { return numWays_; }
};