#!/usr/bin/env python3
"""
Memory Simulator Parallel Runner Script

This script runs a memory simulator with different configurations in parallel.
It organizes results by experiment time, experiment name, and workload.
"""

import os
import sys
import argparse
import subprocess
import multiprocessing
import time
import uuid
import datetime
import itertools
from typing import Tuple, List, Dict

# Default configurations
DEFAULT_PARALLEL_FACTOR = 2
DEFAULT_RESULTS_BASE_DIR = 'experiment-results'
DEFAULT_EXP_NAME = 'memory_simulation'
DEFAULT_APPS_DIR = 'apps'
DEFAULT_COMMON_PARAMS = (30, 32, 512, 1)  # phys_mem, l1_tlb, l2_tlb, pte_cachable
DEFAULT_TOC_CONFIG = [
    (1,8), 
    # (1,4),
    (0,0)
]  # toc_enabled, toc_size

# Page table size configurations
DEFAULT_PAGE_TABLE_SIZES = [
    (512, 512, 512, 512),
    # (8, 2048, 2048, 2048),
    (8, 4096, 4096, 512),
    (4, 2048, 4096, 2048)
    # (16, 2048, 4096, 512),
    # (32, 2048, 2048, 512),
    # (1, 2097152, 128, 256),
]

# Page walk cache (PWC) configurations
DEFAULT_PWC_CONFIGS = [
    # Format: (pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways)
    (4, 4, 4, 4, 16, 4),  # Baseline PWC configuration
    # (4,4,4,4,32,8),
    (16,4,16,4,64,4), # 4 times larger to validate TOC
    # (16,4,16,4,128,16),
]

SPECIAL_WORKLOAD_LIST = [
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '40000']}, # ~19.6GB
    {'name': 'BTree', 'options': ['3000000', '100000']}, #~300MB
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-1']}, #~8.5GB
    {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-1']}, #~8.4GB
    
]

# Define workload lists
HUGE_WORKLOAD_LIST = [
    {'name': 'gups-static', 'options': ['20']}, # ~16GB
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/snb/social_network-sf10-numpart-1']}, #~27.9G
    {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/snb/social_network-sf10-numpart-1']}, #~27.9G
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '40000']}, # ~19.6GB
    {'name': 'BTree', 'options': ['90000000', '100']}, # ~8.75GB

]

LARGE_WORKLOAD_LIST = [
    # {'name': 'seq-list-static', 'options': ['-s', '15', '-e', '15']},
    {'name': 'BTree', 'options': ['90000000', '100']}, # ~8.75GB
    {'name': 'xsbench-static', 'options': ['-t', '1']}, # ~5.5GB
    {'name': 'gups-static', 'options': ['15']}, # ~8GB
    {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.8G
    # {'name': 'graphbig/bfs', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G
    # {'name': 'graphbig/dfs', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.8G
    {'name': 'graphbig/sssp', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.8G
    # {'name': 'graphbig/tc', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G
    # {'name': 'graphbig/cc', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G

]

MIDDLE_WORKLOAD_LIST = [
    {'name': 'BTree', 'options': ['30000000', '100000']}, # ~3.1GB
    {'name': 'xsbench-static', 'options': ['-t', '1']}, # ~2.5GB
    {'name': 'gups-static', 'options': ['5']}, # ~4GB
    # {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.8G
    # {'name': 'graphbig/bfs', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G
    # {'name': 'graphbig/dfs', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.8G
    # {'name': 'graphbig/sssp', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.8G
    # {'name': 'graphbig/tc', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G
    # {'name': 'graphbig/cc', 'options': ['--dataset', '~/workload/graphbig/datagen-7_5-fb', '--separator', '\' \'']}, #~9.6G


]

TINY_WORKLOAD_LIST = [
    # {'name': 'graph500_reference_bfs', 'options': ['20', '20']} #~1.01GB, run even slower
    # {'name': 'seq-list-static', 'options': ['-s', '20', '-e', '20']}, #~697MB, run very slow
    {'name': 'BTree', 'options': ['700000', '100000']}, # ~71MB
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '2000', '-p', '40000']}, #~1.01GB
    {'name': 'gups-static', 'options': ['10']}, # ~8.0G
    {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-2']}, #~4.33GB
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-2']}, #~4.26GB
]

# Base simulator command template
SIMULATOR_BASE_CMD = (
    'time ../../../pin -t obj-intel64/memory_simulator.so '
    '-pgd_pwc_size {pgd_size} '
    '-pgd_pwc_ways {pgd_ways} '
    '-pud_pwc_size {pud_size} '
    '-pud_pwc_ways {pud_ways} '
    '-pmd_pwc_size {pmd_size} '
    '-pmd_pwc_ways {pmd_ways} '
    '-phys_mem_gb {phys_mem} '
    '-l1_tlb_size {l1_tlb} '
    '-l2_tlb_size {l2_tlb} '
    '-pte_cachable {pte_cachable} '
    '-pgd_size {pgd_size_pt} '
    '-pud_size {pud_size_pt} '
    '-pmd_size {pmd_size_pt} '
    '-pte_size {pte_size_pt} '
    '-toc_enabled {toc_enabled} '
    '-toc_size {toc_size} '
    '-o {output_file} '
    '-- {executable} {options}'
)

def generate_page_table_sizes():
    """Generate page table size configurations"""
    return DEFAULT_PAGE_TABLE_SIZES

def generate_pwc_configs():
    """Generate page walk cache configurations"""
    return DEFAULT_PWC_CONFIGS

global_lock = multiprocessing.Lock()
manager = multiprocessing.Manager()
cpu_affinity_counter = manager.Value('i', 0)

def run_one_experiment(config_data):
    """Run a single memory simulator experiment
    
    Parameters:
    - config_data: A tuple containing:
        (pt_config, pwc_config, toc_config, workload, workload_path, base_dir, exp_dir, exp_name, exp_purpose, common_params)
    
    Returns:
    - True if experiment completed successfully, False otherwise
    """
    pt_config, pwc_config, toc_config, workload, workload_path, base_dir, exp_dir, exp_name, exp_purpose, common_params = config_data
    
    # Unpack configurations
    pgd_size_pt, pud_size_pt, pmd_size_pt, pte_size_pt = pt_config
    pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways = pwc_config
    toc_enabled, toc_size = toc_config
    phys_mem, l1_tlb, l2_tlb, pte_cachable = common_params
    
    # Create a unique ID for this run
    config_id = f"pgd{pgd_size_pt}_pud{pud_size_pt}_pmd{pmd_size_pt}_pte{pte_size_pt}_pwc{pgd_size}-{pud_size}-{pmd_size}_toc{toc_enabled}-{toc_size}"
    run_id = f"{config_id}_{uuid.uuid4().hex[:6]}"
    
    # Create workload directory if it doesn't exist
    workload_dir = os.path.join(exp_dir, workload['name'])
    os.makedirs(workload_dir, exist_ok=True)
    
    # Prepare output file path
    output_file = os.path.join(workload_dir, f"output_{run_id}.txt")
    
    try:
        # Change to base directory
        os.chdir(base_dir)
        
        # Prepare workload command
        executable = workload_path
        options = ' '.join(workload['options']) if 'options' in workload else ''
        
        # if we are running "graph500_reference_bfs*", we need to set cpu affinity differently for each thread
        global cpu_affinity_counter
        cpu_affinity = ""
        if workload['name'] == 'graph500_reference_bfs':
            with global_lock:
                
                cpu_affinity = f"taskset -c {cpu_affinity_counter.value % 10} "
                cpu_affinity_counter.value += 1
        # Format the simulator command
        run_cmd = SIMULATOR_BASE_CMD.format(
            pgd_size=pgd_size,
            pgd_ways=pgd_ways,
            pud_size=pud_size,
            pud_ways=pud_ways,
            pmd_size=pmd_size,
            pmd_ways=pmd_ways,
            phys_mem=phys_mem,
            l1_tlb=l1_tlb,
            l2_tlb=l2_tlb,
            pte_cachable=pte_cachable,
            pgd_size_pt=pgd_size_pt,
            pud_size_pt=pud_size_pt,
            pmd_size_pt=pmd_size_pt,
            pte_size_pt=pte_size_pt,
            toc_enabled=toc_enabled,
            toc_size=toc_size,
            output_file=output_file,
            executable=executable,
            options=options
        )

        # Add CPU affinity if needed
        if cpu_affinity:
            run_cmd = f"export SKIP_VALIDATION=1; {cpu_affinity} {run_cmd}"

        # Execute simulator command
        print(f"Running: {workload['name']} with config {config_id}")
        # redirect stderr and stdout to other files
        progress_file = os.path.join(workload_dir, f"progress_{run_id}.txt")
        progress_stream = open(progress_file, 'w')
        # if open failed, use console
        progress_stream = progress_stream if progress_stream else sys.stdout
        run_cmd = f"numactl --cpunodebind=0 --membind=0 {run_cmd}"
        process = subprocess.run(
            run_cmd,
            shell=True,
            check=True,
            stdout=progress_stream,
            stderr=subprocess.STDOUT,
        )
        
        # Create summary file with configuration information
        summary_file = os.path.join(workload_dir, f"summary_{run_id}.txt")
        with open(summary_file, 'w') as f:
            f.write(f"Experiment: {exp_name}\n")
            f.write(f"Workload: {workload['name']}\n")
            f.write(f"Options: {' '.join(workload.get('options', []))}\n\n")
            f.write(f"run command: {run_cmd}\n")
            
            f.write(f"Page Table Configuration:\n")
            f.write(f"  PGD Size: {pgd_size_pt} entries\n")
            f.write(f"  PUD Size: {pud_size_pt} entries\n")
            f.write(f"  PMD Size: {pmd_size_pt} entries\n")
            f.write(f"  PTE Size: {pte_size_pt} entries\n\n")
            
            f.write(f"PWC Configuration:\n")
            f.write(f"  PGD PWC Size: {pgd_size}, Ways: {pgd_ways}\n")
            f.write(f"  PUD PWC Size: {pud_size}, Ways: {pud_ways}\n")
            f.write(f"  PMD PWC Size: {pmd_size}, Ways: {pmd_ways}\n\n")
            
            f.write(f"Other Parameters:\n")
            f.write(f"  Physical Memory: {phys_mem} GB\n")
            f.write(f"  L1 TLB Size: {l1_tlb}\n")
            f.write(f"  L2 TLB Size: {l2_tlb}\n")
            f.write(f"  PTE Cachable: {pte_cachable}\n\n")
            f.write(f"  TOC Enabled: {toc_enabled}\n")
            f.write(f"  TOC Size: {toc_size}\n\n")
            
            # Extract important statistics from output file
            if os.path.exists(output_file):
                with open(output_file, 'r') as out_f:
                    f.write("Key Statistics:\n")
                    statistics_found = False
                    for line in out_f:
                        if any(key in line for key in ['TLB', 'PWC', 'Page', 'Miss', 'Hit', 'Cycles', 'Walk']):
                            f.write(f"  {line.strip()}\n")
                            statistics_found = True
                    
                    if not statistics_found:
                        f.write("  No statistics found in output file\n")
        
        print(f"Completed: {workload['name']} with config {config_id}")
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"Error running {workload['name']} with config {config_id}: {e}")
        if e.stderr:
            print(f"STDERR: {e.stderr.decode()[:200]}...")
        return False
    except Exception as e:
        print(f"Unexpected error with {workload['name']}, config {config_id}: {str(e)}")
        return False
    finally:
        # Change back to script directory
        os.chdir(os.path.join(base_dir, "script"))

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='Run memory simulator with different configurations in parallel')
    parser.add_argument('-t', '--workload_type', type=str, choices=['large', 'tiny', 'middle', 'huge', 'special'], required=True, 
                        help='Workload type: large or tiny')
    parser.add_argument('-n', '--workload_num', type=int, 
                        help='Workload number (optional, if not provided, all workloads will be run)')
    parser.add_argument('-p', '--parallel_factor', type=int, default=DEFAULT_PARALLEL_FACTOR, 
                        help='Parallelism factor (1=num_configs, 2=twice num_configs, etc.)')
    parser.add_argument('-r', '--results_base_dir', type=str, default=DEFAULT_RESULTS_BASE_DIR, 
                        help='Base directory for all experiment results')
    parser.add_argument('-e', '--exp_name', type=str, default=DEFAULT_EXP_NAME, 
                        help='Name of the experiment')
    parser.add_argument('-u', '--exp_purpose', type=str, required=True, 
                        help='Purpose of the experiment (will be written to README)')
    parser.add_argument('-a', '--apps_dir', type=str, default=DEFAULT_APPS_DIR, 
                        help='Directory containing the applications to test')
    args = parser.parse_args()
    
    # Get base directory (parent of script directory)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    base_dir = os.path.dirname(script_dir)
    
    # Get workload base dir
    workload_base_dir = args.apps_dir
    assert os.path.exists(workload_base_dir), f"Workload directory {workload_base_dir} does not exist"
    
    # Create timestamp for the experiment
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # Create experiment directory structure
    results_base_dir = os.path.join(base_dir, args.results_base_dir)
    os.makedirs(results_base_dir, exist_ok=True)
    
    # Create experiment directory with timestamp
    exp_dir_name = f"{timestamp}_{args.exp_name}"
    exp_dir = os.path.join(results_base_dir, exp_dir_name)
    os.makedirs(exp_dir, exist_ok=True)
    
    # Generate configurations
    page_table_configs = generate_page_table_sizes()
    pwc_configs = generate_pwc_configs()
    toc_configs = DEFAULT_TOC_CONFIG  # Add TOC configurations
    
    # Common parameters (fixed for all runs)
    common_params = DEFAULT_COMMON_PARAMS
    
    # Create README.md in the experiment directory
    with open(os.path.join(exp_dir, "README.md"), 'w') as f:
        f.write(f"# Experiment: {args.exp_name}\n\n")
        f.write(f"## Date and Time\n")
        f.write(f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        f.write(f"## Purpose\n")
        f.write(f"{args.exp_purpose}\n\n")
        f.write(f"## Workload Type\n")
        f.write(f"{args.workload_type}\n\n")
        
        f.write(f"## Page Table Size Configurations\n")
        for i, pt_config in enumerate(page_table_configs):
            pgd_size, pud_size, pmd_size, pte_size = pt_config
            f.write(f"### Page Table Config {i+1}:\n")
            f.write(f"- PGD Size: {pgd_size} entries\n")
            f.write(f"- PUD Size: {pud_size} entries\n")
            f.write(f"- PMD Size: {pmd_size} entries\n")
            f.write(f"- PTE Size: {pte_size} entries\n\n")
        
        f.write(f"## PWC Configurations\n")
        for i, pwc_config in enumerate(pwc_configs):
            pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways = pwc_config
            f.write(f"### PWC Config {i+1}:\n")
            f.write(f"- PGD PWC Size: {pgd_size}, Ways: {pgd_ways}\n")
            f.write(f"- PUD PWC Size: {pud_size}, Ways: {pud_ways}\n")
            f.write(f"- PMD PWC Size: {pmd_size}, Ways: {pmd_ways}\n\n")
        
        f.write(f"## Common Parameters\n")
        phys_mem, l1_tlb, l2_tlb, pte_cachable = common_params[:4]
        f.write(f"- Physical Memory: {phys_mem} GB\n")
        f.write(f"- L1 TLB Size: {l1_tlb}\n")
        f.write(f"- L2 TLB Size: {l2_tlb}\n")
        f.write(f"- PTE Cachable: {pte_cachable}\n\n")
        f.write(f"## TOC Parameters\n")
        for toc_enabled, toc_size in toc_configs:
            f.write(f"- TOC Enabled: {toc_enabled}\n")
            f.write(f"- TOC Size: {toc_size}\n\n")
    
    # Select workload list
    # workload_list = LARGE_WORKLOAD_LIST if args.workload_type == 'large' else TINY_WORKLOAD_LIST
   
    if args.workload_type == 'large':
        workload_list = LARGE_WORKLOAD_LIST
    elif args.workload_type == 'huge':
        workload_list = HUGE_WORKLOAD_LIST
    elif args.workload_type == 'middle':
        workload_list = MIDDLE_WORKLOAD_LIST
    elif args.workload_type == 'tiny':
        workload_list = TINY_WORKLOAD_LIST
    elif args.workload_type == 'special':
        workload_list = SPECIAL_WORKLOAD_LIST
    else:
        parser.error("Invalid workload type. Choose 'large', 'middle', 'tiny', 'huge', or 'special'")
        exit(1)
    
    # assert all workloads exist
    for workload in workload_list:
        workload_path = os.path.join(workload_base_dir, workload['name'])
        assert os.path.exists(workload_path), f"Workload directory {workload_path} does not exist"

    # Validate workload number if specified
    if args.workload_num is not None and (args.workload_num < 0 or args.workload_num >= len(workload_list)):
        parser.error(f"Workload number must be between 0 and {len(workload_list)-1}")
    
    # Create experiment configurations with permutations of page table sizes, PWC configs, and TOC configs
    configs = []
    if args.workload_num is not None:
        workload = workload_list[args.workload_num]
        workload_path = os.path.join(workload_base_dir, workload['name'])
        for pt_config in page_table_configs:
            for pwc_config in pwc_configs:
                for toc_config in toc_configs:
                    configs.append((pt_config, pwc_config, toc_config, workload, workload_path, base_dir, exp_dir, 
                                   args.exp_name, args.exp_purpose, common_params))
    else:
        for workload in workload_list:
            workload_path = os.path.join(workload_base_dir, workload['name'])
            for pt_config in page_table_configs:
                for pwc_config in pwc_configs:
                    for toc_config in toc_configs:
                        configs.append((pt_config, pwc_config, toc_config, workload, workload_path, base_dir, exp_dir, 
                                       args.exp_name, args.exp_purpose, common_params))
    
    # Calculate parallelism level
    total_configs = len(page_table_configs) * len(pwc_configs) * len(toc_configs) * len(workload_list)
    num_workers = min(total_configs * args.parallel_factor, len(configs), 8 if args.workload_type == 'huge' or args.workload_type == 'special' else 20)
    print(f"Running {len(configs)} tasks with {num_workers} parallel workers")
    
    # Start timing
    start_time = time.time()
    
    # Run experiments in parallel
    with multiprocessing.Pool(processes=num_workers) as pool:
        results = pool.map(run_one_experiment, configs)
    
    # Report results
    end_time = time.time()
    success_count = sum(1 for r in results if r)
    
    print(f"Completed {success_count} of {len(configs)} tasks successfully")
    print(f"Total execution time: {end_time - start_time:.2f} seconds")
    
    # Generate summary report for the entire experiment
    print("Generating summary report...")
    with open(os.path.join(exp_dir, "experiment_summary.md"), 'w') as f:
        f.write(f"# Experiment Summary: {args.exp_name}\n\n")
        f.write(f"## Overview\n")
        f.write(f"- Date and Time: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"- Completed: {success_count} of {len(configs)} tasks successfully\n")
        f.write(f"- Total execution time: {end_time - start_time:.2f} seconds\n\n")
        
        f.write(f"## Purpose\n")
        f.write(f"{args.exp_purpose}\n\n")
        
        # Add workload information
        f.write("## Results by Workload\n")
        processed_results = 0
        for i, workload in enumerate(workload_list):
            if args.workload_num is not None and i != args.workload_num:
                continue
                
            f.write(f"### Workload {i+1}: {workload['name']}\n")
            if 'options' in workload:
                f.write(f"- Options: {' '.join(workload['options'])}\n")
            
            # Count successful runs for this workload
            # workload_configs = [c for c in configs if c[2]['name'] == workload['name']]
            workload_configs = [c for c in configs if c[3]['name'] == workload['name']]
            workload_result_count = len(workload_configs)
            workload_results = results[processed_results:processed_results + workload_result_count]
            processed_results += workload_result_count
            
            workload_success = sum(1 for r in workload_results if r)
            f.write(f"- Success rate: {workload_success}/{workload_result_count}\n\n")
            
            # Matrix table for page table configurations vs PWC configurations
            f.write("#### Results Matrix\n")
            f.write("| Page Table Config \\ PWC Config | ")
            
            # PWC config headers
            for j, pwc_config in enumerate(pwc_configs):
                pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways = pwc_config
                pwc_id = f"PWC-{j+1}"
                f.write(f"{pwc_id} | ")
            f.write("\n")
            
            # Header separator
            f.write("|" + "-" * 24 + "|")
            for _ in pwc_configs:
                f.write("-" * 8 + "|")
            f.write("\n")
            
            # Page table rows
            for k, pt_config in enumerate(page_table_configs):
                pgd_size, pud_size, pmd_size, pte_size = pt_config
                pt_id = f"PT-{k+1} ({pgd_size},{pud_size},{pmd_size},{pte_size})"
                f.write(f"| {pt_id} | ")
                
                # Results for each combination
                for j, _ in enumerate(pwc_configs):
                    idx = k * len(pwc_configs) + j
                    if idx < len(workload_results):
                        status = "✓" if workload_results[idx] else "✗"
                        f.write(f"{status} | ")
                    else:
                        f.write("N/A | ")
                f.write("\n")
            
            f.write("\n")
    
    print(f"Experiment completed. Results stored in: {exp_dir}")
    print(f"Summary report: {os.path.join(exp_dir, 'experiment_summary.md')}")

if __name__ == '__main__':
    main()