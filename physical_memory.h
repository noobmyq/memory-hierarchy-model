#pragma once

#include <iostream>
#include <vector>
#include "common.h"

// Physical Memory Pool
class PhysicalMemory {
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
    UINT64 AllocateFrame() {
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

    // Get statistics
    UINT64 GetAllocatedFrames() const { return allocatedFrames_; }
    UINT64 GetTotalFrames() const { return frameAllocated_.size(); }
    double GetUtilization() const {
        return static_cast<double>(allocatedFrames_) / frameAllocated_.size();
    }
    UINT64 GetSize() const { return size_; }
};