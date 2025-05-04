#pragma once

#include "cache.h"
#include "common.h"

// Translation Lookaside Buffer (TLB) - maps VPN to PFN
class TLB : public SetAssociativeCache<UINT64, UINT64> {
   protected:
    // Hash function to map VPN to set index
    UINT64 GetSetIndex(const UINT64& vpn) const override {
        return vpn % numSets_;
    }
    void HandleEviction(const UINT64& vpn, const UINT64& pfn,
                        bool dirty) override {
        // No eviction handling needed for TLB
        // (TLB entries are not written back to memory)
    }

   public:
    TLB(const std::string& cacheName = "TLB", UINT64 numEntries = 64,
        UINT64 associativity = 4)
        : SetAssociativeCache<UINT64, UINT64>(
              cacheName,
              numEntries / associativity,  // Sets = Total entries / Ways
              associativity) {}

    // VPN to PFN mapping lookup
    bool Lookup(UINT64 vpn, UINT64& pfn) {
        return SetAssociativeCache<UINT64, UINT64>::Lookup(vpn, pfn);
    }

    // Insert VPN to PFN mapping
    void Insert(UINT64 vpn, UINT64 pfn) {
        SetAssociativeCache<UINT64, UINT64>::Insert(vpn, pfn);
    }
};