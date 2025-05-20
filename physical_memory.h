#pragma once

#include <cassert>
#include <functional>
#include <iostream>
#include <vector>
#include "common.h"
#include "utils/xxhash64.h"

class BasePhysicalMemory {
   public:
    BasePhysicalMemory() = default;
    virtual ~BasePhysicalMemory() = default;

    // Allocate a physical frame
    virtual UINT64 AllocateFrame(UINT64 key, UINT8 keyWidth = 8) = 0;
    virtual std::pair<UINT8, UINT64> AllocateTinyPtrFrame(UINT64 key,
                                                          UINT8 keyWidth) = 0;
    virtual UINT64 DecodeFrame(UINT64 key, UINT8 tinyPointer) = 0;
    // Get statistics
    virtual UINT64 GetAllocatedFrames() const = 0;
    virtual UINT64 GetTotalFrames() const = 0;
    virtual double GetUtilization() const = 0;
    virtual UINT64 GetSize() const = 0;
};

// Physical Memory Pool
class PhysicalMemory : public BasePhysicalMemory {
   private:
    UINT64 size_;                       // Size in bytes
    UINT64 allocatedFrames_;            // Number of allocated frames
    std::vector<bool> frameAllocated_;  // Bitmap of allocated frames
    UINT64 nextFrame_;                  // Next frame to allocate
   public:
    PhysicalMemory(UINT64 memorySize = kPhysicalMemorySize)
        : size_(memorySize), allocatedFrames_(0) {
        UINT64 numFrames = size_ / kMemTracePageSize;
        frameAllocated_.resize(numFrames, false);
        // Reserve frame 0 for null pointer detection
        frameAllocated_[0] = true;
        allocatedFrames_ = 1;
        nextFrame_ = 1;  // Start allocating from frame 1
    }

    // Allocate a physical frame
    UINT64 AllocateFrame(UINT64 key, UINT8 keyWidth = 0) override {
        if (nextFrame_ >= frameAllocated_.size()) {
            // // No free frames available
            // throw std::runtime_error("Physical memory exhausted");
            std::cerr << "Error: Physical memory exhausted. No more frames "
                         "available.\n";
            exit(1);
        }
        frameAllocated_[nextFrame_] = true;
        allocatedFrames_++;
        nextFrame_++;
        return nextFrame_ - 1;
    }

    std::pair<UINT8, UINT64> AllocateTinyPtrFrame(UINT64 key,
                                                  UINT8 keyWidth) override {
        // should not be called
        assert(false && "AllocateTinyPtrFrame should not be called");
    }

    UINT64 DecodeFrame(UINT64 key, UINT8 tinyPointer) override {
        // should not be called
        assert(false && "DecodeFrame should not be called");
    }

    // Get statistics
    UINT64 GetAllocatedFrames() const { return allocatedFrames_; }
    UINT64 GetTotalFrames() const { return frameAllocated_.size(); }
    double GetUtilization() const {
        return static_cast<double>(allocatedFrames_) / frameAllocated_.size();
    }
    UINT64 GetSize() const { return size_; }
};

class MemoryPo2CTable {

   public:
    static constexpr size_t kBinSize = 127;
    static constexpr uint8_t kNullTinyPtr = 0;
    static constexpr uint8_t kOverflowTinyPtr = ((1 << 8) - 1);

    class Bin {
       public:
        Bin();
        ~Bin() = default;

       public:
        bool Full() const;
        UINT8 Count() const;
        // bool Query(UINT64 key, UINT8 ptr, UINT64* valuePtr) const;
        UINT8 Insert(UINT64 key, UINT8 keyWidth);
        bool Update(UINT64 key, UINT8 ptr, UINT64 value);
        bool Free(UINT64 key, UINT8 ptr);

       private:
        UINT8 cnt_ = 0;
        UINT8 head_ = 1;

        // this is not actual frame, just a place holder stored in the bin
        UINT64 bin_[MemoryPo2CTable::kBinSize];
    };

   public:
    MemoryPo2CTable() = delete;

    MemoryPo2CTable(int n) {
        binNum_ =
            (n + MemoryPo2CTable::kBinSize - 1) / MemoryPo2CTable::kBinSize;
        tab_ = new Bin[binNum_];
        srand(time(0));
        int hash_seed[2] = {rand(), rand()};
        while (hash_seed[1] == hash_seed[0])
            hash_seed[1] = rand();
        for (int i = 0; i < 2; ++i)
            hashBin_[i] = std::function<uint64_t(uint64_t)>(
                [=](uint64_t key) -> uint64_t {
                    return SlowXXHash64::hash(&key, sizeof(uint64_t),
                                              hash_seed[i]) %
                           binNum_;
                });
    }

    std::pair<UINT8, UINT64> Allocate(UINT64 key, UINT8 keyWidth) {
        uint64_t hashbin[2];
        for (int i = 0; i < 2; ++i) {
            hashbin[i] = hashBin_[i](key);
        }

        uint8_t flag = tab_[hashbin[0]].Count() > tab_[hashbin[1]].Count();

        uint8_t ptr = tab_[hashbin[flag]].Insert(key, keyWidth);
        // ptr should not be 0 after the previous check in both bins
        assert(ptr);

        UINT64 binIndex = hashBin_[flag](key);
        UINT64 frameNum = binIndex * MemoryPo2CTable::kBinSize + ptr - 1;
        ptr ^=
            flag * (ptr != MemoryPo2CTable::kOverflowTinyPtr) * ((1 << 8) - 1);
        assert(ptr != MemoryPo2CTable::kOverflowTinyPtr);
        assert(ptr != MemoryPo2CTable::kNullTinyPtr);
        return {ptr, frameNum};
    }

    bool Query(UINT64 key, UINT8 ptr, UINT64* valuePtr) {
        assert(ptr != MemoryPo2CTable::kOverflowTinyPtr);
        assert(ptr != MemoryPo2CTable::kNullTinyPtr);

        uint8_t flag = (ptr >= (1 << 7));
        ptr ^= flag * ((1 << 8) - 1);
        UINT64 binIndex = hashBin_[flag](key);
        UINT64 frameNum = binIndex * MemoryPo2CTable::kBinSize + ptr - 1;
        *valuePtr = frameNum;
        return true;
    }

    ~MemoryPo2CTable() = default;

   private:
    std::function<UINT64(UINT64)> hashBin_[2];
    UINT64 binNum_;
    Bin* tab_;
};

class MosaicPhysicalMemory : public BasePhysicalMemory {
   private:
    UINT64 size_;                       // Size in bytes
    UINT64 allocatedFrames_;            // Number of allocated frames
    std::vector<bool> frameAllocated_;  // Bitmap of allocated frames
    MemoryPo2CTable po2cTable_;         // Po2C table for frame allocation

   public:
    MosaicPhysicalMemory(UINT64 memorySize = kPhysicalMemorySize)
        : size_(memorySize),
          allocatedFrames_(0),
          frameAllocated_(size_ / kMemTracePageSize, false),
          po2cTable_(size_ / kMemTracePageSize) {}

    // Allocate a physical frame
    UINT64 AllocateFrame(UINT64 key, UINT8 keyWidth = 8) override {
        if (allocatedFrames_ >= frameAllocated_.size()) {
            // No free frames available
            std::cerr << "Error: Physical memory exhausted. No more frames "
                         "available.\n";
            exit(1);
        }
        auto [ptr, frameNum] = AllocateTinyPtrFrame(key, keyWidth);
        return frameNum;
    }
    std::pair<UINT8, UINT64> AllocateTinyPtrFrame(UINT64 key, UINT8 keyWidth) {
        auto [ptr, frameNum] = po2cTable_.Allocate(key, keyWidth);
        assert(!frameAllocated_[frameNum] && "Frame already allocated");
        frameAllocated_[frameNum] = true;
        allocatedFrames_++;
        return {ptr, frameNum};
    }

    UINT64 DecodeFrame(UINT64 key, UINT8 tinyPointer) override {
        UINT64 frameNum = 0;
        if (!po2cTable_.Query(key, tinyPointer, &frameNum)) {
            std::cerr << "Error: Failed to decode frame.\n";
            exit(1);
        }
        return frameNum;
    }

    // Get statistics
    UINT64 GetAllocatedFrames() const { return allocatedFrames_; }
    UINT64 GetTotalFrames() const { return frameAllocated_.size(); }
    double GetUtilization() const {
        return static_cast<double>(allocatedFrames_) / frameAllocated_.size();
    }
    UINT64 GetSize() const { return size_; }
};