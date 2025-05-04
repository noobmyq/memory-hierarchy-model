#pragma once

#include <iostream>
#include <vector>
#include "common.h"

// Physical Memory Pool
class PhysicalMemory {
   private:
    UINT64 size;                       // Size in bytes
    UINT64 allocatedFrames;            // Number of allocated frames
    std::vector<bool> frameAllocated;  // Bitmap of allocated frames
    UINT64 nextFrame;                  // Next frame to allocate
   public:
    PhysicalMemory(UINT64 memorySize = kPhysicalMemorySize)
        : size(memorySize), allocatedFrames(0) {
        UINT64 numFrames = size / kMemTracePageSize;
        frameAllocated.resize(numFrames, false);
        // Reserve frame 0 for null pointer detection
        frameAllocated[0] = true;
        allocatedFrames = 1;
        nextFrame = 1;  // Start allocating from frame 1
    }

    // Allocate a physical frame
    UINT64 allocateFrame() {
        if (nextFrame >= frameAllocated.size()) {
            // // No free frames available
            // throw std::runtime_error("Physical memory exhausted");
            std::cerr << "Error: Physical memory exhausted. No more frames "
                         "available.\n";
            exit(1);
        }
        frameAllocated[nextFrame] = true;
        allocatedFrames++;
        nextFrame++;
        return nextFrame - 1;
    }

    // Get statistics
    UINT64 getAllocatedFrames() const { return allocatedFrames; }
    UINT64 getTotalFrames() const { return frameAllocated.size(); }
    double getUtilization() const {
        return static_cast<double>(allocatedFrames) / frameAllocated.size();
    }
    UINT64 GetSize() const { return size; }
};