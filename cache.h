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
    size_t numSets;           // Number of sets in the cache
    size_t numWays;           // Number of ways per set (associativity)
    size_t accesses;          // Access counter
    size_t hits;              // Hit counter
    UINT64 globalLruCounter;  // Global counter for LRU policy
    std::vector<std::vector<CacheEntry>> sets;  // Cache storage [set][way]

    // Find the LRU entry in a set
    size_t findLruWay(size_t setIndex) const {
        size_t lruWay = 0;
        UINT64 minCounter = sets[setIndex][0].lruCounter;

        for (size_t way = 0; way < numWays; way++) {
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
    void updateLru(size_t setIndex, size_t wayIndex) {
        sets[setIndex][wayIndex].lruCounter = ++globalLruCounter;
    }

    // Hash function to map from tag to set index
    virtual size_t getSetIndex(const TagType& tag) const = 0;

   protected:
    virtual void handleEviction(const TagType& tag, const ValueType& value,
                                bool dirty) = 0;  // Pure virtual function

   public:
    SetAssociativeCache(const std::string& cacheName, size_t num_sets,
                        size_t ways)
        : name(cacheName),
          numSets(num_sets),
          numWays(ways),
          accesses(0),
          hits(0),
          globalLruCounter(0) {

        // Initialize cache structure
        sets.resize(numSets);
        for (size_t i = 0; i < numSets; i++) {
            sets[i].resize(numWays);
            for (size_t j = 0; j < numWays; j++) {
                sets[i][j].valid = false;
            }
        }
    }

    virtual ~SetAssociativeCache() = default;

    // Core lookup operation
    bool lookup(const TagType& tag, ValueType& value) {
        accesses++;
        size_t setIndex = getSetIndex(tag);

        for (size_t way = 0; way < numWays; way++) {
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
        size_t setIndex = getSetIndex(tag);

        // If block already in cache, update value and mark dirty on write
        for (size_t way = 0; way < numWays; ++way) {
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
        size_t victimWay = findLruWay(setIndex);
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
    size_t getAccesses() const { return accesses; }
    size_t getHits() const { return hits; }
    double getHitRate() const {
        return accesses > 0 ? (double)hits / accesses : 0.0;
    }
    const std::string& getName() const { return name; }
    size_t getSize() const { return numSets * numWays; }
    size_t getNumSets() const { return numSets; }
    size_t getNumWays() const { return numWays; }
};