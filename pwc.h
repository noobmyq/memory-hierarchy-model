#pragma once

#include "cache.h"
#include "common.h"

// Page Walk Cache (PWC) - caches partial translations
class PageWalkCache : public SetAssociativeCache<UINT64, UINT64> {
   private:
    UINT32 indexBitsLow;   // Low bit position for VA tag extraction
    UINT32 indexBitsHigh;  // High bit position for VA tag extraction

   protected:
    // Hash function to map VA tag to set index
    size_t getSetIndex(const UINT64& vaTag) const override {
        return vaTag % numSets;
    }

   public:
    PageWalkCache(const std::string& cacheName, size_t numEntries = 16,
                  size_t associativity = 4, UINT32 lowBit = 0,
                  UINT32 highBit = 63)
        : SetAssociativeCache<UINT64, UINT64>(
              cacheName,
              numEntries / associativity,  // Sets = Total entries / Ways
              associativity),
          indexBitsLow(lowBit),
          indexBitsHigh(highBit) {}

    // Extract tag from virtual address
    UINT64 getTag(ADDRINT vaddr) const {
        UINT64 mask = ((1ULL << (indexBitsHigh - indexBitsLow + 1)) - 1)
                      << indexBitsLow;
        return (vaddr & mask) >> indexBitsLow;
    }

    // Look up translation for a virtual address
    bool lookup(ADDRINT vaddr, UINT64& nextLevelPfn) {
        UINT64 tag = getTag(vaddr);
        return SetAssociativeCache<UINT64, UINT64>::lookup(tag, nextLevelPfn);
    }

    // Insert translation for a virtual address
    void insert(ADDRINT vaddr, UINT64 nextLevelPfn) {
        UINT64 tag = getTag(vaddr);
        SetAssociativeCache<UINT64, UINT64>::insert(tag, nextLevelPfn);
    }

    // Get bit range used for tag extraction
    UINT32 getLowBit() const { return indexBitsLow; }
    UINT32 getHighBit() const { return indexBitsHigh; }
};