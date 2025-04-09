
#pragma once

#include <cstdint>

// Define types to match those in the original simulator
typedef uint64_t ADDRINT;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// Constants for page table
constexpr UINT64 MEMTRACE_PAGE_SIZE = 4096;  // 4KB pages
constexpr UINT32 PAGE_SHIFT = 12;            // log2(MEMTRACE_PAGE_SIZE)
constexpr UINT64 PAGE_MASK =
    MEMTRACE_PAGE_SIZE - 1;  // Mask for offset within page
constexpr UINT64 PHYSICAL_MEMORY_SIZE = 1ULL << 40;  // 1TB physical memory

// Memory reference structure - matches the format in the trace file
struct MEMREF {
    ADDRINT pc;   // Program counter (8 bytes)
    ADDRINT ea;   // Effective address (8 bytes)
    UINT32 size;  // Size of memory access (4 bytes)
    UINT32 read;  // Is this a read (1) or write (0) (4 bytes)
};
// Ensure the struct has no padding
static_assert(sizeof(MEMREF) == 24, "MEMREF struct has unexpected padding");

// make a constexpr version of log2
constexpr UINT32 static_log2(UINT32 n) {
    return (n == 0) ? 0 : (31 - __builtin_clz(n));
}