#pragma once

#include "cache.h"
#include "common.h"

// Page Walk Cache (PWC) - caches partial translations
// if table of contents (TOC) is enabled, the value type is a pointer
class PageWalkCache : public SetAssociativeCache<UINT64, UINT64> {
   private:
    bool TOCEnabled = false;
    UINT32 indexBitsLow;   // Low bit position for VA tag extraction
    UINT32 indexBitsHigh;  // High bit position for VA tag extraction
    UINT32 TOCSize = 4;    // Size of the table of contents (TOC) in bytes
    UINT32 TOCMask = 0;    // Mask for TOC size

    typedef struct TOCEntry {
        bool valid = false;  // Tag for the entry
        UINT64 value = 0;    // Pointer to the next level page table
    } TOCEntry;

   protected:
    // Hash function to map VA tag to set index
    size_t getSetIndex(const UINT64& vaTag) const override {
        return vaTag % numSets;
    }

    void handleEviction(const UINT64& vaTag, const UINT64& pfn,
                        bool dirty) override {
        // No eviction handling needed for PWC
        // (PWC entries are not written back to memory)
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
          indexBitsHigh(highBit) {
        for (size_t i = 0; i < numSets; i++) {
            for (size_t j = 0; j < numWays; j++) {
                sets[i][j].value =
                    (UINT64) nullptr;      // Initialize value to nullptr
                sets[i][j].valid = false;  // Initialize valid bit to false
            }
        }
    }

    void setTOCEnabled(bool enabled) { TOCEnabled = enabled; }
    bool isTOCEnabled() const { return TOCEnabled; }
    void setTOCSize(UINT32 size) {
        TOCSize = size;
        TOCMask = (size - 1) << indexBitsLow;
        indexBitsLow += __builtin_ctz(size);
    }
    UINT32 getTOCSize() const { return TOCSize; }

    // Extract tag from virtual address
    UINT64 getTag(ADDRINT vaddr) const {
        UINT64 mask = ((1ULL << (indexBitsHigh - indexBitsLow + 1)) - 1)
                      << indexBitsLow;
        return (vaddr & mask) >> indexBitsLow;
    }

    // Look up translation for a virtual address
    bool lookup(ADDRINT vaddr, UINT64& nextLevelPfn) {
        UINT64 tag = getTag(vaddr);
        if (TOCEnabled) {
            this->accesses++;
            size_t setIndex = getSetIndex(tag);
            UINT32 TOCIndex =
                (vaddr & TOCMask) >> (indexBitsLow - __builtin_ctz(TOCSize));

            for (size_t way = 0; way < numWays; way++) {
                if (sets[setIndex][way].valid &&
                    sets[setIndex][way].tag == tag) {
                    TOCEntry* TOCPtr = (TOCEntry*)(sets[setIndex][way].value);
                    if (TOCPtr[TOCIndex].valid) {
                        nextLevelPfn = TOCPtr[TOCIndex].value;
                        this->hits++;
                        updateLru(setIndex, way);
                        return true;
                    } else {
                        // Entry is not valid, return false
                        return false;
                    }
                }
            }

            return false;
        }
        return SetAssociativeCache<UINT64, UINT64>::lookup(tag, nextLevelPfn);
    }

    // Insert translation for a virtual address
    void insert(ADDRINT vaddr, UINT64 nextLevelPfn) {
        UINT64 tag = getTag(vaddr);
        if (TOCEnabled) {
            size_t setIndex = getSetIndex(tag);
            UINT32 TOCIndex =
                (vaddr & TOCMask) >> (indexBitsLow - __builtin_ctz(TOCSize));
            for (size_t way = 0; way < numWays; way++) {
                if (sets[setIndex][way].valid &&
                    sets[setIndex][way].tag == tag) {
                    // Entry already exists, update TOC entry
                    TOCEntry* TOCPtr = (TOCEntry*)(sets[setIndex][way].value);
                    // Update TOC entry
                    TOCPtr[TOCIndex].valid = true;
                    TOCPtr[TOCIndex].value = nextLevelPfn;
                    updateLru(setIndex, way);
                    return;
                }
            }

            // Entry does not exist, choose a victim
            size_t way = findLruWay(setIndex);
            bool evictValid = sets[setIndex][way].valid;
            bool evictDirty = false;
            UINT64 evictTag;
            TOCEntry* evictTOCPtr = nullptr;
            if (evictValid) {
                evictTag = sets[setIndex][way].tag;
                evictTOCPtr = (TOCEntry*)(sets[setIndex][way].value);
                evictDirty = evictTOCPtr[TOCIndex].valid;
            }

            // Allocate new TOC entry if needed
            sets[setIndex][way].value =
                (UINT64) new TOCEntry[TOCSize]{};  // Allocate TOC entry
            TOCEntry* TOCPtr = (TOCEntry*)(sets[setIndex][way].value);
            for (UINT32 i = 0; i < TOCSize; i++) {
                TOCPtr[i].valid = false;  // Initialize TOC entry to invalid
            }
            TOCPtr[TOCIndex].valid = true;  // Set the new entry to valid
            TOCPtr[TOCIndex].value = nextLevelPfn;  // Set the new entry value
            sets[setIndex][way].tag = tag;          // Set the tag
            sets[setIndex][way].valid = true;       // Set the entry to valid
            sets[setIndex][way].dirty = false;  // Set the entry to not dirty
            updateLru(setIndex, way);           // Update LRU for the set

            // Handle eviction if needed
            if (evictValid) {
                delete[] evictTOCPtr;  // Free the evicted TOC entry
            }

            return;
        }
        SetAssociativeCache<UINT64, UINT64>::insert(tag, nextLevelPfn);
    }

    // Get bit range used for tag extraction
    UINT32 getLowBit() const { return indexBitsLow; }
    UINT32 getHighBit() const { return indexBitsHigh; }
};