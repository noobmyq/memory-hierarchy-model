#pragma once

#include "cache.h"
#include "common.h"

// Page Walk Cache (PWC) - caches partial translations
// if table of contents (TOC) is enabled, the value type is a pointer
class PageWalkCache : public SetAssociativeCache<UINT64, UINT64> {
   private:
    bool tocEnabled_ = false;
    UINT64 indexBitsLow_;   // Low bit position for VA tag extraction
    UINT64 indexBitsHigh_;  // High bit position for VA tag extraction
    UINT64 tocSize_ = 4;    // Size of the table of contents (TOC) in bytes
    UINT64 tocMask_ = 0;    // Mask for TOC size

    typedef struct TOCEntry {
        bool valid = false;  // Tag for the entry
        UINT64 value = 0;    // Pointer to the next level page table
    } TOCEntry;

   protected:
    // Hash function to map VA tag to set index
    UINT64 GetSetIndex(const UINT64& vaTag) const override {
        return vaTag % numSets_;
    }

    void HandleEviction(const UINT64& vaTag, const UINT64& pfn,
                        bool dirty) override {
        // No eviction handling needed for PWC
        // (PWC entries are not written back to memory)
    }

   public:
    PageWalkCache(const std::string& cacheName, UINT64 numEntries = 16,
                  UINT64 associativity = 4, UINT64 lowBit = 0,
                  UINT64 highBit = 63)
        : SetAssociativeCache<UINT64, UINT64>(
              cacheName,
              numEntries / associativity,  // Sets = Total entries / Ways
              associativity),
          indexBitsLow_(lowBit),
          indexBitsHigh_(highBit) {
        for (UINT64 i = 0; i < numSets_; i++) {
            for (UINT64 j = 0; j < numWays_; j++) {
                sets_[i][j].value =
                    (UINT64) nullptr;       // Initialize value to nullptr
                sets_[i][j].valid = false;  // Initialize valid bit to false
            }
        }
    }

    void SetTocEnabled(bool enabled) { tocEnabled_ = enabled; }
    bool IsTocEnabled() const { return tocEnabled_; }
    void SetTocSize(UINT64 size) {
        tocSize_ = size;
        tocMask_ = ((UINT64)size - 1ULL) << (UINT64)indexBitsLow_;
        indexBitsLow_ += __builtin_ctz(size);
    }
    UINT64 GetTocSize() const { return tocSize_; }

    // Extract tag from virtual address
    UINT64 GetTag(ADDRINT vaddr) const {
        UINT64 mask = ((1ULL << (indexBitsHigh_ - indexBitsLow_ + 1)) - 1)
                      << indexBitsLow_;
        return (vaddr & mask) >> indexBitsLow_;
    }

    // Look up translation for a virtual address
    bool Lookup(ADDRINT vaddr, UINT64& nextLevelPfn) {
        UINT64 tag = GetTag(vaddr);
        if (tocEnabled_) {
            this->accesses_++;
            UINT64 setIndex = GetSetIndex(tag);
            UINT64 tocIndex =
                (vaddr & tocMask_) >> (indexBitsLow_ - __builtin_ctz(tocSize_));

            for (UINT64 way = 0; way < numWays_; way++) {
                if (sets_[setIndex][way].valid &&
                    sets_[setIndex][way].tag == tag) {
                    TOCEntry* tocPtr = (TOCEntry*)(sets_[setIndex][way].value);
                    if (tocPtr[tocIndex].valid) {
                        nextLevelPfn = tocPtr[tocIndex].value;
                        this->hits_++;
                        UpdateLru(setIndex, way);
                        return true;
                    } else {
                        // Entry is not valid, return false
                        return false;
                    }
                }
            }

            return false;
        }
        return SetAssociativeCache<UINT64, UINT64>::Lookup(tag, nextLevelPfn);
    }

    // Insert translation for a virtual address
    void Insert(ADDRINT vaddr, UINT64 nextLevelPfn) {
        UINT64 tag = GetTag(vaddr);
        if (tocEnabled_) {
            UINT64 setIndex = GetSetIndex(tag);
            UINT64 tocIndex =
                (vaddr & tocMask_) >> (indexBitsLow_ - __builtin_ctz(tocSize_));
            for (UINT64 way = 0; way < numWays_; way++) {
                if (sets_[setIndex][way].valid &&
                    sets_[setIndex][way].tag == tag) {
                    // Entry already exists, update TOC entry
                    TOCEntry* tocPtr = (TOCEntry*)(sets_[setIndex][way].value);
                    // Update TOC entry
                    tocPtr[tocIndex].valid = true;
                    tocPtr[tocIndex].value = nextLevelPfn;
                    UpdateLru(setIndex, way);
                    return;
                }
            }

            // Entry does not exist, choose a victim
            UINT64 way = FindLruWay(setIndex);
            bool evictValid = sets_[setIndex][way].valid;
            bool evictDirty = false;
            UINT64 evictTag;
            TOCEntry* evictTOCPtr = nullptr;
            if (evictValid) {
                evictTag = sets_[setIndex][way].tag;
                evictTOCPtr = (TOCEntry*)(sets_[setIndex][way].value);
                evictDirty = evictTOCPtr[tocIndex].valid;
            }

            // Allocate new TOC entry if needed
            sets_[setIndex][way].value =
                (UINT64) new TOCEntry[tocSize_]{};  // Allocate TOC entry
            TOCEntry* tocPtr = (TOCEntry*)(sets_[setIndex][way].value);
            for (UINT64 i = 0; i < tocSize_; i++) {
                tocPtr[i].valid = false;  // Initialize TOC entry to invalid
            }
            tocPtr[tocIndex].valid = true;  // Set the new entry to valid
            tocPtr[tocIndex].value = nextLevelPfn;  // Set the new entry value
            sets_[setIndex][way].tag = tag;         // Set the tag
            sets_[setIndex][way].valid = true;      // Set the entry to valid
            sets_[setIndex][way].dirty = false;  // Set the entry to not dirty
            UpdateLru(setIndex, way);            // Update LRU for the set

            // Handle eviction if needed
            if (evictValid) {
                delete[] evictTOCPtr;  // Free the evicted TOC entry
            }

            return;
        }
        SetAssociativeCache<UINT64, UINT64>::Insert(tag, nextLevelPfn);
    }

    // Get bit range used for tag extraction
    UINT64 GetLowBit() const { return indexBitsLow_; }
    UINT64 GetHighBit() const { return indexBitsHigh_; }
};