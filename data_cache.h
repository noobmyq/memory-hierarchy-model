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


    bool access(ADDRINT paddr, UINT64 &value, bool isWrite) {
        bool hit = lookup(paddr, value);
        
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
            else if(findLruWay(getSetIndex(paddr))) capacityMisses++;
            else conflictMisses++;
        }

        // 插入新条目
        if(!hit) insert(paddr, value);
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

class CacheHierarchy {
    private:
        DataCache l1Cache;
        DataCache l2Cache;
        DataCache l3Cache;
        size_t memAccessCount;
    
    public:
        CacheHierarchy(
            size_t l1Size, size_t l1Ways, size_t l1Line,
            size_t l2Size, size_t l2Ways, size_t l2Line,
            size_t l3Size, size_t l3Ways, size_t l3Line
        ) : 
            l1Cache("L1 Cache", l1Size, l1Ways, l1Line),
            l2Cache("L2 Cache", l2Size, l2Ways, l2Line),
            l3Cache("L3 Cache", l3Size, l3Ways, l3Line),
            memAccessCount(0) {}

        bool lookup(ADDRINT paddr, UINT64& value) {
            // L1访问
            if(l1Cache.lookup(paddr, value)) return true;
            
            // L2访问
            if (l2Cache.lookup(paddr, value)) {
                l1Cache.insert(paddr, value); // 填充L1
                return true;
            }
            
            // L3访问
            if(l3Cache.lookup(paddr, value)) {
                l1Cache.insert(paddr, value); // 填充L1
                l2Cache.insert(paddr, value); // 填充L2
                return true;
            }
            
            return false; // 所有缓存未命中
        }
        
        // 统一访问接口
        bool access(ADDRINT paddr, UINT64& value, bool isWrite) {
            // L1访问
            if(l1Cache.access(paddr, value, isWrite)) return true;
            
            // L2访问
            if (l2Cache.access(paddr, value, isWrite)) {
                
                l1Cache.insert(paddr, value); // 填充L1
                return true;
            }
            
            // L3访问
            if(l3Cache.access(paddr, value, isWrite)) {
                l1Cache.insert(paddr, value); // 填充L1
                l2Cache.insert(paddr, value); // 填充L2
                return true;
            }
            
            // 所有缓存未命中
            memAccessCount++;
            l1Cache.insert(paddr, value); // 填充L1
            l2Cache.insert(paddr, value); // 填充L2
            l3Cache.insert(paddr, value); // 填充L3
            return false;
        }
    
        void printStats(std::ostream& os) const {
            os << "\n=== Cache Hierarchy Statistics ===\n";
            printCacheStats(os, l1Cache);
            printCacheStats(os, l2Cache);
            printCacheStats(os, l3Cache);
            os << "Memory Accesses: " << memAccessCount << "\n";
            os << "Total Access Cost (cycles): " 
               << l1Cache.getAccesses()*1 +  // L1访问周期
                  l2Cache.getAccesses()*4 +  // L2访问周期 
                  l3Cache.getAccesses()*10 + // L3访问周期
                  memAccessCount*100            // 内存访问周期
               << "\n";
        }
    
    private:
        void printCacheStats(std::ostream& os, const DataCache& cache) const {
            os << "[" << cache.getName() << "]\n"
               << "Size: " << cache.getSize()/1024 << "KB\n"
               << "Ways: " << cache.getNumWays() << "\n"
               << "Hit Rate: " << std::fixed << std::setprecision(2)
               << cache.getHitRate()*100 << "%\n"
               << "Accesses: " << cache.getAccesses() << "\n"
               << "Misses: " << cache.getAccesses() - cache.getHits() << "\n\n";
        }
    };