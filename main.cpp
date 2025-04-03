#include "common.h"
#include "page_table.h"
#include "data_cache.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <stdexcept>

struct SimConfig {
    // 基本参数
    std::string trace_file;
    UINT64 phys_mem_gb = 1;
    
    // TLB配置
    struct {
        size_t l1_size = 64;
        size_t l1_ways = 4;
        size_t l2_size = 1024;
        size_t l2_ways = 8;
    } tlb;
    
    // PWC配置
    struct {
        size_t size = 16;
        size_t ways = 4;
    } pwc;
    
    // 缓存层级配置
    struct {
        size_t l1_size = 32*1024;   // 32KB
        size_t l1_ways = 8;
        size_t l1_line = 64;
        size_t l2_size = 256*1024;  // 256KB
        size_t l2_ways = 16;
        size_t l2_line = 64;
        size_t l3_size = 8*1024*1024; // 8MB
        size_t l3_ways = 16;
        size_t l3_line = 64;
    } cache;

    // 物理内存计算
    UINT64 physical_mem_bytes() const {
        return phys_mem_gb * (1ULL << 30);
    }

    // 打印配置
    void print() const {
        std::cout << "\nSimulation Configuration:\n"
                  << "========================\n"
                  << "Trace File:          " << trace_file << "\n"
                  << "Physical Memory:     " << phys_mem_gb << " GB\n"
                  << "L1 TLB:             " << tlb.l1_size << " entries, "
                  << tlb.l1_ways << "-way\n"
                  << "L2 TLB:             " << tlb.l2_size << " entries, "
                  << tlb.l2_ways << "-way\n"
                  << "PWC:                " << pwc.size << " entries, "
                  << pwc.ways << "-way\n"
                  << "L1 Cache:           " << cache.l1_size/1024 << "KB, "
                  << cache.l1_ways << "-way, " << cache.l1_line << "B line\n"
                  << "L2 Cache:           " << cache.l2_size/1024 << "KB, "
                  << cache.l2_ways << "-way, " << cache.l2_line << "B line\n"
                  << "L3 Cache:           " << cache.l3_size/(1024*1024) << "MB, "
                  << cache.l3_ways << "-way, " << cache.l3_line << "B line\n"
                  << std::endl;
    }
};
SimConfig parse_arguments(int argc, char* argv[]) {
    SimConfig config;

    // Check for help or insufficient arguments
    if (argc < 2 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: " << argv[0] << " <trace_file> [options]\n"
                  << "Options:\n"
                  << "  --phys_mem_gb <GB>         Physical memory size (default: 1)\n"
                  << "  --l1_tlb_size <entries>    L1 TLB size (default: 64)\n"
                  << "  --l1_tlb_ways <ways>       L1 TLB associativity (default: 4)\n"
                  << "  --l2_tlb_size <entries>    L2 TLB size (default: 1024)\n"
                  << "  --l2_tlb_ways <ways>       L2 TLB associativity (default: 8)\n"
                  << "  --pwc_size <entries>       PWC size (default: 16)\n"
                  << "  --pwc_ways <ways>          PWC associativity (default: 4)\n"
                  << "  --l1_cache_size <bytes>    L1 Cache size (default: 32768)\n"
                  << "  --l1_ways <ways>           L1 Cache associativity (default: 8)\n"
                  << "  --l1_line <bytes>          L1 Cache line size (default: 64)\n"
                  << "  --l2_cache_size <bytes>    L2 Cache size (default: 262144)\n"
                  << "  --l2_ways <ways>           L2 Cache associativity (default: 16)\n"
                  << "  --l2_line <bytes>          L2 Cache line size (default: 64)\n"
                  << "  --l3_cache_size <bytes>    L3 Cache size (default: 8388608)\n"
                  << "  --l3_ways <ways>           L3 Cache associativity (default: 16)\n"
                  << "  --l3_line <bytes>          L3 Cache line size (default: 64)\n";
        throw std::invalid_argument(argc < 2 ? "Missing trace file" : "Help requested");
    }

    config.trace_file = argv[1];

    // Parse flags
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            throw std::invalid_argument("Missing value for " + std::string(argv[i]));
        }
        std::string flag = argv[i];
        size_t value;
        try {
            value = std::stoul(argv[i + 1]);
        } catch (...) {
            throw std::invalid_argument("Invalid value for " + flag);
        }

        if (flag == "--phys_mem_gb") config.phys_mem_gb = value;
        else if (flag == "--l1_tlb_size") config.tlb.l1_size = value;
        else if (flag == "--l1_tlb_ways") config.tlb.l1_ways = value;
        else if (flag == "--l2_tlb_size") config.tlb.l2_size = value;
        else if (flag == "--l2_tlb_ways") config.tlb.l2_ways = value;
        else if (flag == "--pwc_size") config.pwc.size = value;
        else if (flag == "--pwc_ways") config.pwc.ways = value;
        else if (flag == "--l1_cache_size") config.cache.l1_size = value;
        else if (flag == "--l1_ways") config.cache.l1_ways = value;
        else if (flag == "--l1_line") config.cache.l1_line = value;
        else if (flag == "--l2_cache_size") config.cache.l2_size = value;
        else if (flag == "--l2_ways") config.cache.l2_ways = value;
        else if (flag == "--l2_line") config.cache.l2_line = value;
        else if (flag == "--l3_cache_size") config.cache.l3_size = value;
        else if (flag == "--l3_ways") config.cache.l3_ways = value;
        else if (flag == "--l3_line") config.cache.l3_line = value;
        else {
            throw std::invalid_argument("Unknown flag: " + flag);
        }
    }

    return config;
}
int main(int argc, char* argv[]) try {
    SimConfig config = parse_arguments(argc, argv);
    config.print();

    // 初始化组件
    PhysicalMemory physical_memory(config.physical_mem_bytes());
    CacheHierarchy cache_hierarchy(
        config.cache.l1_size, config.cache.l1_ways, config.cache.l1_line,
        config.cache.l2_size, config.cache.l2_ways, config.cache.l2_line,
        config.cache.l3_size, config.cache.l3_ways, config.cache.l3_line
    );
    
    PageTable page_table(
        physical_memory, cache_hierarchy,
        config.tlb.l1_size, config.tlb.l1_ways,
        config.tlb.l2_size, config.tlb.l2_ways,
        config.pwc.size, config.pwc.ways
    );

    // 打开trace文件
    std::ifstream input(config.trace_file, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open trace file: " + config.trace_file);
    }

    // 处理内存引用
    std::unordered_map<UINT64, size_t> virtual_pages, physical_pages;
    size_t access_count = 0;
    MEMREF ref;

    while (input.read(reinterpret_cast<char*>(&ref), sizeof(MEMREF))) {
        if (input.gcount() != sizeof(MEMREF)) break;

        access_count++;
        const ADDRINT vaddr = ref.ea;
        const ADDRINT paddr = page_table.translate(vaddr);

        // 访问缓存层级
        UINT64 value = 0;
        cache_hierarchy.access(paddr, value, ref.read);

        // 统计页面使用
        virtual_pages[vaddr / PAGE_SIZE]++;
        physical_pages[paddr / PAGE_SIZE]++;

        // 进度显示
        if (access_count % 1'000'000 == 0) {
            std::cout << "Processed " << (access_count / 1'000'000) << "M accesses\r";
        }
    }

    // 输出统计结果
    std::cout << "\n\nSimulation Results:\n"
              << "==================\n"
              << "Total accesses:       " << access_count << "\n"
              << "Unique virtual pages: " << virtual_pages.size() << "\n"
              << "Unique physical pages:" << physical_pages.size() << "\n"
              << "Physical memory used: " 
              << (physical_pages.size() * PAGE_SIZE) / (1024.0 * 1024) 
              << " MB\n";

    page_table.printDetailedStats(std::cout);
    page_table.printMemoryStats(std::cout);
    cache_hierarchy.printStats(std::cout);

    return 0;
}
catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
}