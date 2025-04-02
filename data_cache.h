// data_cache.h
#pragma once

#include <iostream>
#include <iomanip>
#include "common.h"
#include "cache.h"

class DataCache : public SetAssociativeCache<UINT64, UINT64> {
private:
    UINT32 lineSize;
    UINT32 offsetBits;
    size_t readAccesses;
    size_t readHits;
    size_t writeAccesses;
    size_t writeHits;
    size_t writebacks;
    size_t coldMisses;
    size_t capacityMisses;
    size_t conflictMisses;

    static UINT32 log2(UINT32 n) {
        if (n == 0) return 0;
        UINT32 result = 0;
        while (n >>= 1) result++;
        return result;
    }

protected:
    size_t getSetIndex(const UINT64& paddr) const override {
        UINT64 index = (paddr >> offsetBits) & (numSets - 1);
        return index;
    }

public:
    DataCache(const std::string& name, size_t totalSize, size_t associativity, UINT32 lineSize)
        : SetAssociativeCache<UINT64, UINT64>(name, totalSize / (associativity * lineSize), associativity),
          lineSize(lineSize) {
      offsetBits = log2(lineSize);
      readAccesses = readHits = 0;
      writeAccesses = writeHits = 0;
      writebacks = coldMisses = capacityMisses = conflictMisses = 0;
    }

    UINT64 getTag(ADDRINT paddr) const {
        return paddr >> (offsetBits + log2(numSets));
    }

    bool access(ADDRINT paddr, bool isWrite) {
        UINT64 tag = getTag(paddr);
        UINT64 dummy;
        bool hit = lookup(tag, dummy);
        
        // 更新详细统计
        if(isWrite) {
            writeAccesses++;
            if(hit) writeHits++;
        } else {
            readAccesses++;
            if(hit) readHits++;
        }

        // 未命中类型分析
        if(!hit) {
            if(globalLruCounter < numSets*numWays) coldMisses++;
            else if(findLruWay(getSetIndex(tag))) capacityMisses++;
            else conflictMisses++;
        }

        // 插入新条目
        if(!hit) insert(tag, 0);
        return hit;
    }

    // 新增统计方法
    double getReadHitRate() const { 
        return readAccesses ? (double)readHits/readAccesses : 0; 
    }
    double getWriteHitRate() const { 
        return writeAccesses ? (double)writeHits/writeAccesses : 0; 
    }
    void printDetailedStats(std::ostream& os) const {
        os << "\nData Cache Detailed Statistics:\n";
        os << "==============================\n";
        os << std::left << std::setw(25) << "Total Accesses" 
           << std::right << std::setw(15) << getAccesses() << "\n";
        os << std::left << std::setw(25) << "Read Accesses"
           << std::right << std::setw(15) << readAccesses << "\n";
        os << std::left << std::setw(25) << "Read Hit Rate"
           << std::setw(15) << std::fixed << std::setprecision(2) << getReadHitRate()*100 << "%\n";
        os << std::left << std::setw(25) << "Write Accesses"
           << std::right << std::setw(15) << writeAccesses << "\n";
        os << std::left << std::setw(25) << "Write Hit Rate" 
           << std::setw(15) << std::fixed << std::setprecision(2) << getWriteHitRate()*100 << "%\n";
        os << std::left << std::setw(25) << "Cold Misses"
           << std::right << std::setw(15) << coldMisses << "\n";
        os << std::left << std::setw(25) << "Capacity Misses"
           << std::right << std::setw(15) << capacityMisses << "\n";
        os << std::left << std::setw(25) << "Conflict Misses"
           << std::right << std::setw(15) << conflictMisses << "\n";
        os << std::left << std::setw(25) << "Writebacks"
           << std::right << std::setw(15) << writebacks << "\n";
    }
};