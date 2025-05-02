#pragma once

#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
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

    std::string name;         // Cache name for reporting
    UINT64 numSets;           // Number of sets in the cache
    UINT64 numWays;           // Number of ways per set (associativity)
    UINT64 accesses;          // Access counter
    UINT64 hits;              // Hit counter
    UINT64 globalLruCounter;  // Global counter for LRU policy
    std::vector<std::vector<CacheEntry>> sets;  // Cache storage [set][way]

    // Find the LRU entry in a set
    UINT64 findLruWay(UINT64 setIndex) const {
        UINT64 lruWay = 0;
        UINT64 minCounter = sets[setIndex][0].lruCounter;

        for (UINT64 way = 0; way < numWays; way++) {
            if (!sets[setIndex][way].valid) {
                return way;  // Return first invalid entry
            }
            if (sets[setIndex][way].lruCounter < minCounter) {
                minCounter = sets[setIndex][way].lruCounter;
                lruWay = way;
            }
        }
        return lruWay;
    }

    // Update LRU status for an entry
    void updateLru(UINT64 setIndex, UINT64 wayIndex) {
        sets[setIndex][wayIndex].lruCounter = ++globalLruCounter;
    }

    // Hash function to map from tag to set index
    virtual UINT64 getSetIndex(const TagType& tag) const = 0;

   protected:
    virtual void handleEviction(const TagType& tag, const ValueType& value,
                                bool dirty) = 0;  // Pure virtual function

   public:
    SetAssociativeCache(const std::string& cacheName, UINT64 num_sets,
                        UINT64 ways)
        : name(cacheName),
          numSets(num_sets),
          numWays(ways),
          accesses(0),
          hits(0),
          globalLruCounter(0) {

        // Initialize cache structure
        sets.resize(numSets);
        for (UINT64 i = 0; i < numSets; i++) {
            sets[i].resize(numWays);
            for (UINT64 j = 0; j < numWays; j++) {
                sets[i][j].valid = false;
            }
        }
    }

    virtual ~SetAssociativeCache() = default;

    // Core lookup operation
    bool lookup(const TagType& tag, ValueType& value) {
        accesses++;
        UINT64 setIndex = getSetIndex(tag);

        for (UINT64 way = 0; way < numWays; way++) {
            if (sets[setIndex][way].valid && sets[setIndex][way].tag == tag) {
                hits++;
                value = sets[setIndex][way].value;
                updateLru(setIndex, way);
                return true;
            }
        }
        return false;
    }

    // In cache.h, inside SetAssociativeCache class:
    void insert(const TagType& tag, const ValueType& value,
                bool isWrite = false) {
        UINT64 setIndex = getSetIndex(tag);

        // If block already in cache, update value and mark dirty on write
        for (UINT64 way = 0; way < numWays; ++way) {
            if (sets[setIndex][way].valid && sets[setIndex][way].tag == tag) {
                sets[setIndex][way].value = value;
                if (isWrite) {
                    sets[setIndex][way].dirty = true;  // mark as modified
                }
                // (If not a write, leave dirty flag as is)
                updateLru(setIndex, way);
                return;
            }
        }

        // Block not present – choose a victim to evict (LRU)
        UINT64 victimWay = findLruWay(setIndex);
        bool evictValid = sets[setIndex][victimWay].valid;
        bool evictDirty = false;
        TagType evictTag;
        ValueType evictValue;
        if (evictValid) {
            // Save evicted block info for write-back
            evictDirty = sets[setIndex][victimWay].dirty;
            evictTag = sets[setIndex][victimWay].tag;
            evictValue = sets[setIndex][victimWay].value;
        }

        // Replace victim with new block
        sets[setIndex][victimWay].tag = tag;
        sets[setIndex][victimWay].value = value;
        sets[setIndex][victimWay].valid = true;
        sets[setIndex][victimWay].dirty =
            isWrite;  // dirty if this is a write access
        updateLru(setIndex, victimWay);

        // Write-back to next level if we evicted a dirty block
        if (evictValid && evictDirty) {
            handleEviction(evictTag, evictValue, true);
        }
    }

    // Stats reporting methods
    UINT64 getAccesses() const { return accesses; }
    UINT64 getHits() const { return hits; }
    double getHitRate() const {
        return accesses > 0 ? (double)hits / accesses : 0.0;
    }
    const std::string& getName() const { return name; }
    UINT64 getSize() const { return numSets * numWays; }
    UINT64 getNumSets() const { return numSets; }
    UINT64 getNumWays() const { return numWays; }
};