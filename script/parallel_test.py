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
from typing import Tuple, List, Dict

# Define workload lists
LARGE_WORKLOAD_LIST = [
    {'name': 'BTree', 'options': ['700000', '100000']},
    # {'name': 'mcf-static', 'options': ['inp.in']},
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '2000', '-p', '40000']},
    {'name': 'seq-list-static', 'options': ['-s', '15', '-e', '15']},
]

TINY_WORKLOAD_LIST = [
    {'name': 'hello-static'},
    {'name': 'BTree', 'options': ['1', '1']},
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '2', '-p', '3']},
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
    '-o {output_file} '
    '-- {executable} {options}'
)

def generate_simulator_configs():
    """Generate memory simulator configurations"""
    return [
        # Format: (pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways, phys_mem, l1_tlb, l2_tlb, pte_cachable)
        (16, 4, 16, 4, 96, 4, 30, 32, 512, 0),
        (32, 8, 16, 4, 96, 4, 30, 32, 512, 0),
        (16, 4, 32, 8, 96, 4, 30, 32, 512, 0),
        (16, 4, 16, 4, 128, 8, 30, 32, 512, 0),
        (32, 8, 32, 8, 128, 8, 30, 32, 512, 0),
    ]

def run_one_experiment(config_data):
    """Run a single memory simulator experiment
    
    Parameters:
    - config_data: A tuple containing:
        (sim_config, workload, workload_path, base_dir, exp_dir, exp_name, exp_purpose)
    
    Returns:
    - True if experiment completed successfully, False otherwise
    """
    sim_config, workload, workload_path, base_dir, exp_dir, exp_name, exp_purpose = config_data
    
    # Unpack configuration
    pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways, phys_mem, l1_tlb, l2_tlb, pte_cachable = sim_config
    
    # Create a unique ID for this run
    run_id = f"{pgd_size}_{pud_size}_{pmd_size}_{uuid.uuid4().hex[:6]}"
    
    # Create workload directory if it doesn't exist
    workload_dir = os.path.join(exp_dir, workload['name'])
    os.makedirs(workload_dir, exist_ok=True)
    
    # Prepare output file path
    output_file = os.path.join(workload_dir, f"output_{run_id}.txt")
    
    try:
        # Change to simulator directory (assuming the script is in a script/ subfolder)
        simulator_dir = os.path.join(base_dir)
        os.chdir(simulator_dir)
        
        # Prepare workload command
        executable = workload_path
        options = ' '.join(workload['options']) if 'options' in workload else ''
        
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
            output_file=output_file,
            executable=executable,
            options=options
        )
        
        # Execute simulator command
        print(f"Running: {workload['name']} with config {sim_config}")
        process = subprocess.run(
            run_cmd,
            shell=True,
            check=True,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE
        )
        
        # Create summary file with configuration information
        summary_file = os.path.join(workload_dir, f"summary_{run_id}.txt")
        with open(summary_file, 'w') as f:
            f.write(f"Experiment: {exp_name}\n")
            f.write(f"Workload: {workload['name']}\n")
            f.write(f"Options: {' '.join(workload.get('options', []))}\n")
            f.write(f"Configuration:\n")
            f.write(f"  PGD PWC Size: {pgd_size}, Ways: {pgd_ways}\n")
            f.write(f"  PUD PWC Size: {pud_size}, Ways: {pud_ways}\n")
            f.write(f"  PMD PWC Size: {pmd_size}, Ways: {pmd_ways}\n")
            f.write(f"  Physical Memory: {phys_mem} GB\n")
            f.write(f"  L1 TLB Size: {l1_tlb}\n")
            f.write(f"  L2 TLB Size: {l2_tlb}\n")
            f.write(f"  PTE Cachable: {pte_cachable}\n\n")
            
            # Extract important statistics from output file
            if os.path.exists(output_file):
                with open(output_file, 'r') as out_f:
                    f.write("Key Statistics:\n")
                    statistics_found = False
                    for line in out_f:
                        if any(key in line for key in ['TLB', 'PWC', 'Page', 'Miss', 'Hit', 'Cycles']):
                            f.write(f"  {line.strip()}\n")
                            statistics_found = True
                    
                    if not statistics_found:
                        f.write("  No statistics found in output file\n")
        
        print(f"Completed: {workload['name']} with config {sim_config}")
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"Error running {workload['name']} with config {sim_config}: {e}")
        if e.stderr:
            print(f"STDERR: {e.stderr.decode()[:200]}...")
        return False
    except Exception as e:
        print(f"Unexpected error with {workload['name']}, config {sim_config}: {str(e)}")
        return False
    finally:
        # Change back to script directory
        os.chdir(os.path.join(base_dir, "script"))

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='Run memory simulator with different configurations in parallel')
    parser.add_argument('-t', '--workload_type', type=str, choices=['large', 'tiny'], required=True, 
                        help='Workload type: large or tiny')
    parser.add_argument('-n', '--workload_num', type=int, 
                        help='Workload number (optional, if not provided, all workloads will be run)')
    parser.add_argument('-p', '--parallel_factor', type=int, default=2, 
                        help='Parallelism factor (1=num_configs, 2=twice num_configs, etc.)')
    parser.add_argument('-r', '--results_base_dir', type=str, default='experiment-results', 
                        help='Base directory for all experiment results')
    parser.add_argument('-e', '--exp_name', type=str, default='memory_simulation', 
                        help='Name of the experiment')
    parser.add_argument('-u', '--exp_purpose', type=str, required=True, 
                        help='Purpose of the experiment (will be written to README)')
    parser.add_argument('-a', '--apps_dir', type=str, default='apps', 
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
    
    # Create README.md in the experiment directory
    with open(os.path.join(exp_dir, "README.md"), 'w') as f:
        f.write(f"# Experiment: {args.exp_name}\n\n")
        f.write(f"## Date and Time\n")
        f.write(f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        f.write(f"## Purpose\n")
        f.write(f"{args.exp_purpose}\n\n")
        f.write(f"## Workload Type\n")
        f.write(f"{args.workload_type}\n\n")
        f.write(f"## Configuration Summary\n")
        
        # Add configurations to README
        configs = generate_simulator_configs()
        for i, config in enumerate(configs):
            pgd_size, pgd_ways, pud_size, pud_ways, pmd_size, pmd_ways, phys_mem, l1_tlb, l2_tlb, pte_cachable = config
            f.write(f"### Config {i+1}:\n")
            f.write(f"- PGD PWC Size: {pgd_size}, Ways: {pgd_ways}\n")
            f.write(f"- PUD PWC Size: {pud_size}, Ways: {pud_ways}\n")
            f.write(f"- PMD PWC Size: {pmd_size}, Ways: {pmd_ways}\n")
            f.write(f"- Physical Memory: {phys_mem} GB\n")
            f.write(f"- L1 TLB Size: {l1_tlb}\n")
            f.write(f"- L2 TLB Size: {l2_tlb}\n")
            f.write(f"- PTE Cachable: {pte_cachable}\n\n")
    
    # Select workload list
    workload_list = LARGE_WORKLOAD_LIST if args.workload_type == 'large' else TINY_WORKLOAD_LIST
    
    # assert all workloads exist
    for workload in workload_list:
        workload_path = os.path.join(workload_base_dir, workload['name'])
        assert os.path.exists(workload_path), f"Workload directory {workload_path} does not exist"

    # Validate workload number if specified
    if args.workload_num is not None and (args.workload_num < 0 or args.workload_num >= len(workload_list)):
        parser.error(f"Workload number must be between 0 and {len(workload_list)-1}")
    
    # Generate simulator configurations
    sim_config_list = generate_simulator_configs()
    
    # Create experiment configurations
    configs = []
    if args.workload_num is not None:
        workload = workload_list[args.workload_num]
        workload_path = os.path.join(workload_base_dir, workload['name'])
        for config in sim_config_list:
            configs.append((config, workload, workload_path, base_dir, exp_dir, args.exp_name, args.exp_purpose))
    else:
        for workload in workload_list:
            workload_path = os.path.join(workload_base_dir, workload['name'])
            for config in sim_config_list:
                configs.append((config, workload, workload_path, base_dir, exp_dir, args.exp_name, args.exp_purpose))
    
    # Calculate parallelism level
    num_workers = min(len(sim_config_list) * args.parallel_factor, len(configs))
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
        f.write("## Workload Summary\n")
        for i, workload in enumerate(workload_list):
            if args.workload_num is not None and i != args.workload_num:
                continue
                
            f.write(f"### Workload {i+1}: {workload['name']}\n")
            if 'options' in workload:
                f.write(f"- Options: {' '.join(workload['options'])}\n")
            
            # Count successful runs for this workload
            workload_configs = [c for c in configs if c[1]['name'] == workload['name']]
            workload_results = results[:len(workload_configs)]
            results = results[len(workload_configs):]  # Remove processed results
            
            workload_success = sum(1 for r in workload_results if r)
            f.write(f"- Success rate: {workload_success}/{len(workload_configs)}\n\n")
    
    print(f"Experiment completed. Results stored in: {exp_dir}")
    print(f"Summary report: {os.path.join(exp_dir, 'experiment_summary.md')}")

if __name__ == '__main__':
    main()