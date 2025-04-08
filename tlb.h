#pragma once

#include "cache.h"
#include "common.h"

// Translation Lookaside Buffer (TLB) - maps VPN to PFN
class TLB : public SetAssociativeCache<UINT64, UINT64> {
   protected:
    // Hash function to map VPN to set index
    size_t getSetIndex(const UINT64& vpn) const override {
        return vpn % numSets;
    }
    void handleEviction(const UINT64& vpn, const UINT64& pfn,
                        bool dirty) override {
        // No eviction handling needed for TLB
        // (TLB entries are not written back to memory)
    }

   public:
    TLB(const std::string& cacheName = "TLB", size_t numEntries = 64,
        size_t associativity = 4)
        : SetAssociativeCache<UINT64, UINT64>(
              cacheName,
              numEntries / associativity,  // Sets = Total entries / Ways
              associativity) {}

    // VPN to PFN mapping lookup
    bool lookup(UINT64 vpn, UINT64& pfn) {
        return SetAssociativeCache<UINT64, UINT64>::lookup(vpn, pfn);
    }

    // Insert VPN to PFN mapping
    void insert(UINT64 vpn, UINT64 pfn) {
        SetAssociativeCache<UINT64, UINT64>::insert(vpn, pfn);
    }
};