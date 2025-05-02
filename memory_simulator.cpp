#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include "common.h"
#include "data_cache.h"
#include "page_table.h"
#include "pin.H"

using std::cerr;
using std::cout;
using std::endl;

static_assert(sizeof(MEMREF) == 24, "MEMREF struct has unexpected padding");

// --- Simulator Class ---
class Simulator {
   public:
    Simulator(const SimConfig& config,
              std::unique_ptr<std::ofstream> out_stream = nullptr)
        : config_(config),
          physical_memory_(config.physical_mem_bytes()),
          cache_hierarchy_(
              config.cache.l1_size, config.cache.l1_ways, config.cache.l1_line,
              config.cache.l2_size, config.cache.l2_ways, config.cache.l2_line,
              config.cache.l3_size, config.cache.l3_ways, config.cache.l3_line),
          page_table_(
              physical_memory_, cache_hierarchy_, config.pgtbl.pte_cachable,
              config.tlb.l1_size, config.tlb.l1_ways, config.tlb.l2_size,
              config.tlb.l2_ways, config.pwc.pgdSize, config.pwc.pgdWays,
              config.pwc.pudSize, config.pwc.pudWays, config.pwc.pmdSize,
              config.pwc.pmdWays, config.pgtbl.pgd_size, config.pgtbl.pud_size,
              config.pgtbl.pmd_size, config.pgtbl.pte_size,
              config.pgtbl.TOCEnabled, config.pgtbl.TOCSize),
          out_stream_(std::move(out_stream)) {}
    void process_batch(const MEMREF* buffer, UINT64 numElements) {
        for (UINT64 i = 0; i < numElements; ++i) {
            const MEMREF& ref = buffer[i];
            access_count_++;
            const ADDRINT vaddr = ref.ea;
            const ADDRINT paddr = page_table_.translate(vaddr);
            UINT64 value = 0;
            cache_hierarchy_.access(paddr, value, !ref.read);

            // UINT64 vpn = vaddr / MEMTRACE_PAGE_SIZE;
            // UINT64 ppn = paddr / MEMTRACE_PAGE_SIZE;
            // virtual_pages_[vpn]++;
            // physical_pages_[ppn]++;

            if (access_count_ % 10000000 == 0) {
                cout << "Processed " << (access_count_ / 10000000)
                     << "*10M accesses\r" << std::flush;
            }
        }
    }

    void print_stats() {
        // cout << "\n\nSimulation Results:\n"
        //      << "==================\n"
        //      << "Total accesses:       " << access_count_ << "\n"
        //      << "Unique virtual pages: " << virtual_pages_.size() << "\n"
        //      << "Unique physical pages:" << physical_pages_.size() << "\n"
        //      << "Physical memory used: "
        //      << (physical_pages_.size() * MEMTRACE_PAGE_SIZE) / (1024.0 * 1024)
        //      << " MB\n";
        page_table_.printDetailedStats(*out_stream_);
        page_table_.printMemoryStats(*out_stream_);
        cache_hierarchy_.printStats(*out_stream_);
    }

   private:
    SimConfig config_;
    PhysicalMemory physical_memory_;
    CacheHierarchy cache_hierarchy_;
    PageTable page_table_;
    UINT64 access_count_ = 0;
    std::unique_ptr<std::ofstream> out_stream_;
};

// --- Pin Configuration ---
#define NUM_BUF_PAGES 1024
BUFFER_ID bufId;

// --- Knobs for Simulator Configuration ---
KNOB<UINT64> KnobPhysMemGB(KNOB_MODE_WRITEONCE, "pintool", "phys_mem_gb", "1",
                           "Physical memory size in GB");
KNOB<UINT64> KnobL1TLBSize(KNOB_MODE_WRITEONCE, "pintool", "l1_tlb_size", "64",
                           "L1 TLB size");
KNOB<UINT64> KnobL1TLBWays(KNOB_MODE_WRITEONCE, "pintool", "l1_tlb_ways", "4",
                           "L1 TLB associativity");
KNOB<UINT64> KnobL2TLBSize(KNOB_MODE_WRITEONCE, "pintool", "l2_tlb_size",
                           "1024", "L2 TLB size");
KNOB<UINT64> KnobL2TLBWays(KNOB_MODE_WRITEONCE, "pintool", "l2_tlb_ways", "8",
                           "L2 TLB associativity");
KNOB<UINT64> KnobPGDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pgd_pwc_size",
                            "16", "PWC size");
KNOB<UINT64> KnobPGDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pgd_pwc_ways", "4",
                            "PWC associativity");
KNOB<UINT64> KnobPUDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pud_pwc_size",
                            "16", "PWC size");
KNOB<UINT64> KnobPUDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pud_pwc_ways", "4",
                            "PWC associativity");
KNOB<UINT64> KnobPMDPWCSize(KNOB_MODE_WRITEONCE, "pintool", "pmd_pwc_size",
                            "16", "PWC size");
KNOB<UINT64> KnobPMDPWCWays(KNOB_MODE_WRITEONCE, "pintool", "pmd_pwc_ways", "4",
                            "PWC associativity");
KNOB<UINT64> KnobL1CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l1_cache_size",
                             "32768", "L1 Cache size in bytes");
KNOB<UINT64> KnobL1Ways(KNOB_MODE_WRITEONCE, "pintool", "l1_ways", "8",
                        "L1 Cache associativity");
KNOB<UINT64> KnobL1Line(KNOB_MODE_WRITEONCE, "pintool", "l1_line", "64",
                        "L1 Cache line size");
KNOB<UINT64> KnobL2CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l2_cache_size",
                             "262144", "L2 Cache size in bytes");
KNOB<UINT64> KnobL2Ways(KNOB_MODE_WRITEONCE, "pintool", "l2_ways", "16",
                        "L2 Cache associativity");
KNOB<UINT64> KnobL2Line(KNOB_MODE_WRITEONCE, "pintool", "l2_line", "64",
                        "L2 Cache line size");
KNOB<UINT64> KnobL3CacheSize(KNOB_MODE_WRITEONCE, "pintool", "l3_cache_size",
                             "8388608", "L3 Cache size in bytes");
KNOB<UINT64> KnobL3Ways(KNOB_MODE_WRITEONCE, "pintool", "l3_ways", "16",
                        "L3 Cache associativity");
KNOB<UINT64> KnobL3Line(KNOB_MODE_WRITEONCE, "pintool", "l3_line", "64",
                        "L3 Cache line size");
KNOB<bool> KnobPteCachable(KNOB_MODE_WRITEONCE, "pintool", "pte_cachable", "0",
                           "PTE cacheable flag");
KNOB<UINT64> KnobPGDSize(KNOB_MODE_WRITEONCE, "pintool", "pgd_size", "512",
                         "Number of PGD entries");
KNOB<UINT64> KnobPUDSize(KNOB_MODE_WRITEONCE, "pintool", "pud_size", "512",
                         "Number of PUD entries");
KNOB<UINT64> KnobPMDSize(KNOB_MODE_WRITEONCE, "pintool", "pmd_size", "512",
                         "Number of PMD entries");
KNOB<UINT64> KnobPTESize(KNOB_MODE_WRITEONCE, "pintool", "pte_size", "512",
                         "Number of PTE entries");
KNOB<bool> KnobTOCEnabled(KNOB_MODE_WRITEONCE, "pintool", "toc_enabled", "0",
                          "Enable Table of Contents (TOC) for PWC");
KNOB<UINT32> KnobTOCSize(KNOB_MODE_WRITEONCE, "pintool", "toc_size", "0",
                         "Size of the Table of Contents (TOC) in bytes");
KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o",
                                 "memory_simulator.out",
                                 "Output file for simulation results");

// --- Pin Instrumentation ---
VOID Trace(TRACE trace, VOID* v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            if (!INS_IsStandardMemop(ins))
                continue;
            UINT32 memOps = INS_MemoryOperandCount(ins);
            for (UINT32 memOp = 0; memOp < memOps; memOp++) {
                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp,
                                         offsetof(MEMREF, ea), IARG_UINT32,
                                         INS_MemoryOperandSize(ins, memOp),
                                         offsetof(MEMREF, size), IARG_BOOL,
                                         INS_MemoryOperandIsRead(ins, memOp),
                                         offsetof(MEMREF, read), IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp,
                                         offsetof(MEMREF, ea), IARG_UINT32,
                                         INS_MemoryOperandSize(ins, memOp),
                                         offsetof(MEMREF, size), IARG_BOOL,
                                         INS_MemoryOperandIsRead(ins, memOp),
                                         offsetof(MEMREF, read), IARG_END);
                }
            }
        }
    }
}

// --- Buffer Handling ---
VOID* BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT* ctx, VOID* buf,
                 UINT64 numElements, VOID* v) {
    Simulator* simulator = static_cast<Simulator*>(v);
    simulator->process_batch(static_cast<MEMREF*>(buf), numElements);
    return buf;
}

// --- Fini Function ---
VOID Fini(INT32 code, VOID* v) {
    Simulator* simulator = static_cast<Simulator*>(v);
    simulator->print_stats();
    delete simulator;
}

// --- Usage ---
INT32 Usage() {
    cerr << "This Pin tool instruments a program and simulates its memory "
            "hierarchy.\n";
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

// --- Main ---
int main(int argc, char* argv[]) {
    if (PIN_Init(argc, argv))
        return Usage();

    // Configure simulator using knobs
    SimConfig config;
    config.phys_mem_gb = KnobPhysMemGB.Value();
    config.tlb.l1_size = KnobL1TLBSize.Value();
    config.tlb.l1_ways = KnobL1TLBWays.Value();
    config.tlb.l2_size = KnobL2TLBSize.Value();
    config.tlb.l2_ways = KnobL2TLBWays.Value();
    config.pwc.pgdSize = KnobPGDPWCSize.Value();
    config.pwc.pgdWays = KnobPGDPWCWays.Value();
    config.pwc.pudSize = KnobPUDPWCSize.Value();
    config.pwc.pudWays = KnobPUDPWCWays.Value();
    config.pwc.pmdSize = KnobPMDPWCSize.Value();
    config.pwc.pmdWays = KnobPMDPWCWays.Value();
    config.cache.l1_size = KnobL1CacheSize.Value();
    config.cache.l1_ways = KnobL1Ways.Value();
    config.cache.l1_line = KnobL1Line.Value();
    config.cache.l2_size = KnobL2CacheSize.Value();
    config.cache.l2_ways = KnobL2Ways.Value();
    config.cache.l2_line = KnobL2Line.Value();
    config.cache.l3_size = KnobL3CacheSize.Value();
    config.cache.l3_ways = KnobL3Ways.Value();
    config.cache.l3_line = KnobL3Line.Value();
    config.pgtbl.pte_cachable = (KnobPteCachable.Value() != 0);
    config.pgtbl.pgd_size = KnobPGDSize.Value();
    config.pgtbl.pud_size = KnobPUDSize.Value();
    config.pgtbl.pmd_size = KnobPMDSize.Value();
    config.pgtbl.pte_size = KnobPTESize.Value();
    config.pgtbl.TOCEnabled = KnobTOCEnabled.Value();
    config.pgtbl.TOCSize = KnobTOCSize.Value();

    // Open output file
    auto out_file = std::make_unique<std::ofstream>(KnobOutputFile.Value());
    if (!out_file->is_open()) {
        cerr << "Error: Unable to open output file " << KnobOutputFile.Value()
             << endl;
        return 1;
    }

    config.print(*out_file);

    // Initialize simulator
    Simulator* simulator = new Simulator(config, std::move(out_file));

    // Define trace buffer
    bufId = PIN_DefineTraceBuffer(sizeof(MEMREF), NUM_BUF_PAGES, BufferFull,
                                  simulator);
    if (bufId == BUFFER_ID_INVALID) {
        cerr << "Error: Buffer initialization failed" << endl;
        return 1;
    }

    // Register instrumentation and fini functions
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, simulator);

    // Start the program
    PIN_StartProgram();
    return 0;
}