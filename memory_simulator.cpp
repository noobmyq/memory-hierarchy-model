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
          physical_memory_(config.PhysicalMemBytes()),
          cache_hierarchy_(
              config.cache.l1Size, config.cache.l1Ways, config.cache.l1Line,
              config.cache.l2Size, config.cache.l2Ways, config.cache.l2Line,
              config.cache.l3Size, config.cache.l3Ways, config.cache.l3Line),
          page_table_(
              physical_memory_, cache_hierarchy_, config.pgtbl.pteCachable,
              config.tlb.l1Size, config.tlb.l1Ways, config.tlb.l2Size,
              config.tlb.l2Ways, config.pwc.pgdSize, config.pwc.pgdWays,
              config.pwc.pudSize, config.pwc.pudWays, config.pwc.pmdSize,
              config.pwc.pmdWays, config.pgtbl.pgdSize, config.pgtbl.pudSize,
              config.pgtbl.pmdSize, config.pgtbl.pteSize,
              config.pgtbl.tocEnabled, config.pgtbl.tocSize),
          out_stream_(std::move(out_stream)) {}
    void process_batch(const MEMREF* buffer, UINT64 numElements) {
        for (UINT64 i = 0; i < numElements; ++i) {
            const MEMREF& ref = buffer[i];
            access_count_++;
            const ADDRINT vaddr = ref.ea;
            const ADDRINT paddr = page_table_.Translate(vaddr);
            UINT64 value = 0;
            cache_hierarchy_.Access(paddr, value, !ref.read);

            // UINT64 vpn = vaddr / kMemTracePageSize;
            // UINT64 ppn = paddr / kMemTracePageSize;
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
        //      << (physical_pages_.size() * kMemTracePageSize) / (1024.0 * 1024)
        //      << " MB\n";
        page_table_.PrintDetailedStats(*out_stream_);
        page_table_.PrintMemoryStats(*out_stream_);
        cache_hierarchy_.PrintStats(*out_stream_);
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
KNOB<UINT64> KnobTOCSize(KNOB_MODE_WRITEONCE, "pintool", "toc_size", "0",
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
            UINT64 memOps = INS_MemoryOperandCount(ins);
            for (UINT64 memOp = 0; memOp < memOps; memOp++) {
                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp,
                                         offsetof(MEMREF, ea), IARG_UINT64,
                                         INS_MemoryOperandSize(ins, memOp),
                                         offsetof(MEMREF, size), IARG_BOOL,
                                         INS_MemoryOperandIsRead(ins, memOp),
                                         offsetof(MEMREF, read), IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp,
                                         offsetof(MEMREF, ea), IARG_UINT64,
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
    config.physMemGb = KnobPhysMemGB.Value();
    config.tlb.l1Size = KnobL1TLBSize.Value();
    config.tlb.l1Ways = KnobL1TLBWays.Value();
    config.tlb.l2Size = KnobL2TLBSize.Value();
    config.tlb.l2Ways = KnobL2TLBWays.Value();
    config.pwc.pgdSize = KnobPGDPWCSize.Value();
    config.pwc.pgdWays = KnobPGDPWCWays.Value();
    config.pwc.pudSize = KnobPUDPWCSize.Value();
    config.pwc.pudWays = KnobPUDPWCWays.Value();
    config.pwc.pmdSize = KnobPMDPWCSize.Value();
    config.pwc.pmdWays = KnobPMDPWCWays.Value();
    config.cache.l1Size = KnobL1CacheSize.Value();
    config.cache.l1Ways = KnobL1Ways.Value();
    config.cache.l1Line = KnobL1Line.Value();
    config.cache.l2Size = KnobL2CacheSize.Value();
    config.cache.l2Ways = KnobL2Ways.Value();
    config.cache.l2Line = KnobL2Line.Value();
    config.cache.l3Size = KnobL3CacheSize.Value();
    config.cache.l3Ways = KnobL3Ways.Value();
    config.cache.l3Line = KnobL3Line.Value();
    config.pgtbl.pteCachable = (KnobPteCachable.Value() != 0);
    config.pgtbl.pgdSize = KnobPGDSize.Value();
    config.pgtbl.pudSize = KnobPUDSize.Value();
    config.pgtbl.pmdSize = KnobPMDSize.Value();
    config.pgtbl.pteSize = KnobPTESize.Value();
    config.pgtbl.tocEnabled = KnobTOCEnabled.Value();
    config.pgtbl.tocSize = KnobTOCSize.Value();

    // Open output file
    auto out_file = std::make_unique<std::ofstream>(KnobOutputFile.Value());
    if (!out_file->is_open()) {
        cerr << "Error: Unable to open output file " << KnobOutputFile.Value()
             << endl;
        return 1;
    }

    config.Print(*out_file);

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