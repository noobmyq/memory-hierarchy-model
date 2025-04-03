
#pragma once

#include <cstdint>

// Define types to match those in the original simulator
typedef uint64_t ADDRINT;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

// Constants for page table
constexpr UINT64 PAGE_SIZE = 4096;           // 4KB pages
constexpr UINT32 PAGE_SHIFT = 12;            // log2(PAGE_SIZE)
constexpr UINT64 PAGE_MASK = PAGE_SIZE - 1;  // Mask for offset within page
constexpr UINT32 PTE_ENTRIES = 512;          // Number of entries per page table
constexpr UINT32 PTE_SHIFT = 9;              // log2(PTE_ENTRIES)
constexpr UINT32 PTE_MASK =
    PTE_ENTRIES - 1;  // Mask for index within page table
constexpr UINT64 PHYSICAL_MEMORY_SIZE = 1ULL << 40;  // 1TB physical memory

// Bit positions for different page table levels
constexpr int PGD_SHIFT = PAGE_SHIFT + 3 * PTE_SHIFT;  // 39
constexpr int PUD_SHIFT = PAGE_SHIFT + 2 * PTE_SHIFT;  // 30
constexpr int PMD_SHIFT = PAGE_SHIFT + PTE_SHIFT;      // 21
constexpr int PTE_INDEX_SHIFT = PAGE_SHIFT;            // 12

// Memory reference structure - matches the format in the trace file
struct MEMREF {
    ADDRINT pc;   // Program counter (8 bytes)
    ADDRINT ea;   // Effective address (8 bytes)
    UINT32 size;  // Size of memory access (4 bytes)
    UINT32 read;  // Is this a read (1) or write (0) (4 bytes)
};
// Ensure the struct has no padding
static_assert(sizeof(MEMREF) == 24, "MEMREF struct has unexpected padding");