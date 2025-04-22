#!/usr/bin/env python3
"""
Result Gatherer for Memory Simulator Experiments (Pandas Version)

This script gathers results from memory simulator experiments and compiles them
into CSV files for easier analysis using pandas DataFrames.
"""

import os
import re
import argparse
import glob
import pandas as pd
from pathlib import Path

def parse_output_file(file_path):
    """
    Parse a memory simulator output file and extract relevant metrics.
    
    Args:
        file_path: Path to the output file
        
    Returns:
        A dictionary containing the extracted metrics
    """
    metrics = {}
    
    # Try to open and read the file
    try:
        with open(file_path, 'r') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading file {file_path}: {e}")
        return metrics
    
    # Extract configuration info
    # Physical Memory
    match = re.search(r'Physical Memory:\s+(\d+)\s+GB', content)
    if match:
        metrics['phys_mem_gb'] = match.group(1)
        
    # L1 TLB
    match = re.search(r'L1 TLB:\s+(\d+)\s+entries,\s+(\d+)-way', content)
    if match:
        metrics['l1_tlb_entries'] = match.group(1)
        metrics['l1_tlb_ways'] = match.group(2)
    
    # L2 TLB
    match = re.search(r'L2 TLB:\s+(\d+)\s+entries,\s+(\d+)-way', content)
    if match:
        metrics['l2_tlb_entries'] = match.group(1)
        metrics['l2_tlb_ways'] = match.group(2)
    
    # PWC configurations
    match = re.search(r'Page Walk Cache \(PGD\):\s+(\d+)\s+entries,\s+(\d+)-way', content)
    if match:
        metrics['pgd_pwc_entries'] = match.group(1)
        metrics['pgd_pwc_ways'] = match.group(2)
        
    match = re.search(r'Page Walk Cache \(PUD\):\s+(\d+)\s+entries,\s+(\d+)-way', content)
    if match:
        metrics['pud_pwc_entries'] = match.group(1)
        metrics['pud_pwc_ways'] = match.group(2)
        
    match = re.search(r'Page Walk Cache \(PMD\):\s+(\d+)\s+entries,\s+(\d+)-way', content)
    if match:
        metrics['pmd_pwc_entries'] = match.group(1)
        metrics['pmd_pwc_ways'] = match.group(2)
    
    # Page table sizes
    match = re.search(r'PGD Size:\s+(\d+)\s+entries', content)
    if match:
        metrics['pgd_size'] = match.group(1)
        
    match = re.search(r'PUD Size:\s+(\d+)\s+entries', content)
    if match:
        metrics['pud_size'] = match.group(1)
        
    match = re.search(r'PMD Size:\s+(\d+)\s+entries', content)
    if match:
        metrics['pmd_size'] = match.group(1)
        
    match = re.search(r'PTE Size:\s+(\d+)\s+entries', content)
    if match:
        metrics['pte_size'] = match.group(1)
    
    # TOC parameters
    match = re.search(r'TOC Enabled:\s+(\w+)', content)
    if match:
        metrics['toc_enabled'] = match.group(1)
        
    match = re.search(r'TOC Size:\s+(\d+)', content)
    if match:
        metrics['toc_size'] = match.group(1)
    
    # Extract translation path statistics
    # L1 TLB hits (0 memory requests)
    match = re.search(r'L1 TLB Hit\s+(\d+)\s+([\d\.]+)%', content)
    if match:
        metrics['l1_tlb_hit_count'] = int(match.group(1))
    else:
        metrics['l1_tlb_hit_count'] = 0
    
    # L2 TLB hits (0 memory requests)
    match = re.search(r'L2 TLB Hit\s+(\d+)\s+([\d\.]+)%', content)
    if match:
        metrics['l2_tlb_hit_count'] = int(match.group(1))
    else:
        metrics['l2_tlb_hit_count'] = 0
    
    # PMD PWC hits (1 memory request)
    match = re.search(r'PMD PWC Hit\s+(\d+)\s+([\d\.]+)%', content)
    if match:
        metrics['pmd_pwc_hit_count'] = int(match.group(1))
    else:
        metrics['pmd_pwc_hit_count'] = 0
    
    # PUD PWC hits (2 memory requests)
    match = re.search(r'PUD PWC Hit\s+(\d+)\s+([\d\.]+)%', content)
    if match:
        metrics['pud_pwc_hit_count'] = int(match.group(1))
    else:
        metrics['pud_pwc_hit_count'] = 0
    
    # PGD PWC hits (3 memory requests)
    match = re.search(r'PGD PWC Hit\s+(\d+)\s+([\d\.]+)%', content)
    if match:
        metrics['pgd_pwc_hit_count'] = int(match.group(1))
    else:
        metrics['pgd_pwc_hit_count'] = 0
    
    # Full page walks (4 memory requests)
    match = re.search(r'Full Page Walk\s+(\d+)\s+([\d\.]+)?%?', content)
    if match:
        metrics['full_page_walk_count'] = int(match.group(1))
    else:
        metrics['full_page_walk_count'] = 0
    
    # Total translations
    match = re.search(r'Total Translations\s+(\d+)', content)
    if match:
        metrics['total_translations'] = int(match.group(1))
    
    # TLB Efficiency
    match = re.search(r'TLB Efficiency:\s+([\d\.]+)%', content)
    if match:
        metrics['tlb_efficiency'] = match.group(1)
    
    # Extract cache statistics (the correct hit rates and access counts)
    # L1 TLB hit rate and accesses
    match = re.search(r'L1 TLB\s+\d+\s+\d+\s+\d+\s+(\d+)\s+\d+\s+([\d\.]+)%', content)
    if match:
        metrics['l1_tlb_accesses'] = int(match.group(1))
        metrics['l1_tlb_hit_percentage'] = match.group(2)
        
        # Use L1 TLB accesses as the total number of translations if not found earlier
        if 'total_translations' not in metrics:
            metrics['total_translations'] = metrics['l1_tlb_accesses']
    
    # L2 TLB hit rate and accesses
    match = re.search(r'L2 TLB\s+\d+\s+\d+\s+\d+\s+(\d+)\s+\d+\s+([\d\.]+)%', content)
    if match:
        metrics['l2_tlb_accesses'] = int(match.group(1))
        metrics['l2_tlb_hit_percentage'] = match.group(2)
    
    # PGD hit rate and accesses
    match = re.search(r'PML4E Cache \(PGD\)\s+\d+\s+\d+\s+\d+\s+(\d+)\s+\d+\s+([\d\.]+)%', content)
    if match:
        metrics['pgd_pwc_accesses'] = int(match.group(1))
        metrics['pgd_pwc_hit_percentage'] = match.group(2)
    
    # PUD hit rate and accesses
    match = re.search(r'PDPTE Cache \(PUD\)\s+\d+\s+\d+\s+\d+\s+(\d+)\s+\d+\s+([\d\.]+)%', content)
    if match:
        metrics['pud_pwc_accesses'] = int(match.group(1))
        metrics['pud_pwc_hit_percentage'] = match.group(2)
    
    # PMD hit rate and accesses
    match = re.search(r'PDE Cache \(PMD\)\s+\d+\s+\d+\s+\d+\s+(\d+)\s+\d+\s+([\d\.]+)%', content)
    if match:
        metrics['pmd_pwc_accesses'] = int(match.group(1))
        metrics['pmd_pwc_hit_percentage'] = match.group(2)
    
    # Extract page table statistics
    match = re.search(r'Total page tables:\s+(\d+)', content)
    if match:
        metrics['total_page_tables'] = match.group(1)
    
    match = re.search(r'Total memory for page tables:\s+([\d\.]+)\s+MB', content)
    if match:
        metrics['page_table_memory_mb'] = match.group(1)
    
    # Extract cache access statistics
    match = re.search(r'Page Table Entry data Cache Hits\s+(\d+)', content)
    if match:
        metrics['pte_cache_hits'] = int(match.group(1))
    
    match = re.search(r'Page Table Entry data Cache Misses\s+(\d+)', content)
    if match:
        metrics['pte_cache_misses'] = int(match.group(1))
    
    match = re.search(r'Page Walk Memory Accesses\s+(\d+)', content)
    if match:
        metrics['page_walk_memory_accesses'] = int(match.group(1))
    
    match = re.search(r'Page Table Entry Cache hits ratio\s+([\d\.]+)%', content)
    if match:
        metrics['pte_cache_hit_ratio'] = match.group(1)
    
    # Extract memory cost statistics
    match = re.search(r'Memory Accesses:\s+(\d+)', content)
    if match:
        metrics['memory_accesses'] = int(match.group(1))
    
    match = re.search(r'Total Access Cost \(cycles\):\s+(\d+)', content)
    if match:
        metrics['total_access_cost_cycles'] = match.group(1)
    
    # Calculate average memory requests per translation based on path statistics
    # Each path has a different memory request count:
    # - L1 TLB Hit: 0 memory requests
    # - L2 TLB Hit: 0 memory requests
    # - PMD PWC Hit: 1 memory request (miss L1 & L2 TLB, access PMD)
    # - PUD PWC Hit: 2 memory requests (miss L1 & L2 TLB, miss PMD, access PUD)
    # - PGD PWC Hit: 3 memory requests (miss L1 & L2 TLB, miss PMD, miss PUD, access PGD)
    # - Full Page Walk: 4 memory requests (miss all caches, do full memory walk)
    
    if 'total_translations' in metrics and metrics['total_translations'] > 0:
        # Multiply each path count by its memory request count
        l1_tlb_memory_requests = 0 * metrics['l1_tlb_hit_count']
        l2_tlb_memory_requests = 0 * metrics['l2_tlb_hit_count']
        pmd_pwc_memory_requests = 1 * metrics['pmd_pwc_hit_count']
        pud_pwc_memory_requests = 2 * metrics['pud_pwc_hit_count']
        pgd_pwc_memory_requests = 3 * metrics['pgd_pwc_hit_count']
        full_walk_memory_requests = 4 * metrics['full_page_walk_count']
        
        # Sum up all memory requests
        total_memory_requests = (l1_tlb_memory_requests + l2_tlb_memory_requests + 
                                pmd_pwc_memory_requests + pud_pwc_memory_requests + 
                                pgd_pwc_memory_requests + full_walk_memory_requests)
        
        # Calculate average
        avg_memory_requests = total_memory_requests / metrics['total_translations']
        metrics['avg_memory_requests_per_translation'] = round(avg_memory_requests, 6)
    
    # Calculate average memory request per walk, the denominator is now pmd pwc accesses
    if 'pmd_pwc_accesses' in metrics and metrics['pmd_pwc_accesses'] > 0:
        avg_memory_requests_per_walk = total_memory_requests / metrics['pmd_pwc_accesses']
        metrics['avg_memory_requests_per_walk'] = round(avg_memory_requests_per_walk, 6)

    # Convert numeric metrics back to strings for consistency
    for key in metrics:
        if isinstance(metrics[key], (int, float)):
            metrics[key] = str(metrics[key])
    
    return metrics

def extract_config_from_path(file_path):
    """
    Extract configuration information from the file path
    
    Args:
        file_path: Path to the output file
        
    Returns:
        A dictionary with configuration information
    """
    config = {}
    
    # Convert to Path object to make path manipulation easier
    path = Path(file_path)
    
    # Extract experiment name and workload from path
    parts = path.parts
    
    # Find the timestamp directory by looking for the pattern
    timestamp_idx = -1
    for i, part in enumerate(parts):
        if re.match(r'\d{8}_\d{6}_', part):
            timestamp_idx = i
            break
    
    if timestamp_idx >= 0:
        timestamp_exp_name = parts[timestamp_idx]
        
        # Parse timestamp and experiment name
        match = re.match(r'(\d{8}_\d{6})_(.+)', timestamp_exp_name)
        if match:
            config['timestamp'] = match.group(1)
            config['experiment_name'] = match.group(2)
        
        # The workload is the last directory in the path before the output file
        # For nested workloads like graphbig/bfs, just take the last part as the workload name
        workload_path = "/".join(parts[timestamp_idx+1:-1])
        config['workload'] = parts[-2]  # Just the last directory name before the file
        config['full_workload_path'] = workload_path  # Keep the full path for reference
    
    # Extract config ID from filename - we'll keep parsing these but they'll be excluded in the final output
    match = re.search(r'output_pgd(\d+)_pud(\d+)_pmd(\d+)_pte(\d+)_pwc(\d+)-(\d+)-(\d+)', path.name)
    if match:
        config['pgd_size_pt'] = match.group(1)
        config['pud_size_pt'] = match.group(2)
        config['pmd_size_pt'] = match.group(3)
        config['pte_size_pt'] = match.group(4)
        config['pgd_pwc'] = match.group(5)
        config['pud_pwc'] = match.group(6)
        config['pmd_pwc'] = match.group(7)
    
    return config

def read_experiment_purpose(readme_path):
    """
    Read the experiment purpose from a README.md file
    
    Args:
        readme_path: Path to the README.md file
        
    Returns:
        The experiment purpose as a string
    """
    purpose = ""
    if Path(readme_path).exists():
        try:
            with open(readme_path, 'r') as f:
                content = f.read()
                match = re.search(r'## Purpose\n(.*?)(?=\n\n|\Z)', content, re.DOTALL)
                if match:
                    purpose = match.group(1).strip()
        except Exception as e:
            print(f"Error reading README {readme_path}: {e}")
    return purpose

def gather_results(base_dir):
    """
    Gather results from all experiment directories
    
    Args:
        base_dir: Base directory containing experiment results
        
    Returns:
        A pandas DataFrame containing all the results
    """
    # Lists to collect data for DataFrame
    all_data = []
    
    # Find all experiment directories
    base_path = Path(base_dir)
    exp_dirs = [d for d in base_path.iterdir() if d.is_dir() and re.match(r'\d{8}_\d{6}_', d.name)]
    
    for exp_dir in exp_dirs:
        # Get experiment purpose
        purpose = read_experiment_purpose(exp_dir / "README.md")
        
        # Find all output files recursively
        output_files = list(exp_dir.glob("**/output_*.txt"))
        
        for output_file in output_files:
            try:
                # Parse output file
                metrics = parse_output_file(output_file)
                if not metrics:
                    print(f"Warning: No metrics extracted from {output_file}")
                    continue
                
                # Extract config from path
                config = extract_config_from_path(output_file)
                
                # Add experiment purpose
                config['purpose'] = purpose
                
                # Add summary file path for reference
                summary_file = str(output_file).replace("output_", "summary_")
                if Path(summary_file).exists():
                    config['summary_file'] = str(Path(summary_file).relative_to(base_path))
                
                # Combine config and metrics
                result = {**config, **metrics}
                all_data.append(result)
                
            except Exception as e:
                print(f"Error processing {output_file}: {e}")
    
    # Convert to DataFrame
    if all_data:
        df = pd.DataFrame(all_data)
        return df
    else:
        print("No results found.")
        return pd.DataFrame()

def define_column_order():
    """
    Define a logical order for columns in the DataFrame
    
    Returns:
        A list of column names in the desired order
    """
    return [
        # Experiment info
        'timestamp', 'experiment_name', 'purpose', 'workload',
        
        # Configuration
        'pgd_size', 'pud_size', 'pmd_size', 'pte_size',
        'pgd_pwc_entries', 'pgd_pwc_ways',
        'pud_pwc_entries', 'pud_pwc_ways',
        'pmd_pwc_entries', 'pmd_pwc_ways',
        'phys_mem_gb',
        'l1_tlb_entries', 'l1_tlb_ways',
        'l2_tlb_entries', 'l2_tlb_ways',
        'toc_enabled', 'toc_size',
        
        # TLB statistics
        'l1_tlb_hit_count', 'l1_tlb_accesses', 'l1_tlb_hit_percentage',
        'l2_tlb_hit_count', 'l2_tlb_accesses', 'l2_tlb_hit_percentage',
        'tlb_efficiency',
        
        # PWC statistics
        'pgd_pwc_hit_count', 'pgd_pwc_accesses', 'pgd_pwc_hit_percentage',
        'pud_pwc_hit_count', 'pud_pwc_accesses', 'pud_pwc_hit_percentage',
        'pmd_pwc_hit_count', 'pmd_pwc_accesses', 'pmd_pwc_hit_percentage',
        
        # Memory request statistics
        'avg_memory_requests_per_translation',
        
        # Page table statistics
        'total_page_tables', 'page_table_memory_mb',
        
        # Cache statistics
        'pte_cache_hits', 'pte_cache_misses',
        'page_walk_memory_accesses', 'pte_cache_hit_ratio',
        
        # Memory statistics
        'memory_accesses', 'total_access_cost_cycles',
        
        # Reference
        'summary_file'
    ]

def save_dataframe_with_ordered_columns(df, output_file):
    """
    Save a DataFrame to CSV with columns in a logical order
    
    Args:
        df: Pandas DataFrame to save
        output_file: Path to save the CSV file
    """
    # Define the desired column order
    ordered_columns = define_column_order()
    
    # Get existing columns in the DataFrame
    df_columns = set(df.columns)
    
    # Filter the ordered columns to include only those in the DataFrame
    filtered_columns = [col for col in ordered_columns if col in df_columns]
    
    # Add any remaining columns from the DataFrame that weren't in our ordered list,
    # excluding the columns we want to remove
    columns_to_exclude = ['pgd_size_pt', 'pud_size_pt', 'pmd_size_pt', 'pte_size_pt', 
                          'pgd_pwc', 'pud_pwc', 'pmd_pwc', 'full_workload_path']
    remaining_columns = sorted(list(df_columns - set(filtered_columns) - set(columns_to_exclude)))
    final_columns = filtered_columns + remaining_columns
    
    # Reorder the DataFrame columns and save to CSV
    df = df[final_columns]
    df.to_csv(output_file, index=False)
    print(f"Saved {len(df)} rows to {output_file}")

def create_timestamp_csvs(df, output_dir):
    """
    Create separate CSV files for each timestamp experiment
    
    Args:
        df: Pandas DataFrame containing all results
        output_dir: Directory to save the CSV files
    """
    if df.empty:
        print("No data to generate timestamp CSVs.")
        return
    
    # Create output directory if it doesn't exist
    Path(output_dir).mkdir(parents=True, exist_ok=True)
    
    # Group by timestamp
    if 'timestamp' not in df.columns:
        print("Error: DataFrame does not contain 'timestamp' column.")
        return
    
    # For each timestamp, create a separate CSV
    for timestamp, group_df in df.groupby('timestamp'):
        output_file = Path(output_dir) / f"experiment_{timestamp}.csv"
        save_dataframe_with_ordered_columns(group_df, output_file)
    
    print(f"Generated {df['timestamp'].nunique()} timestamp-specific CSV files in {output_dir}")

def create_workload_csvs(df, output_dir):
    """
    Create separate CSV files for each workload
    
    Args:
        df: Pandas DataFrame containing all results
        output_dir: Directory to save the CSV files
    """
    if df.empty:
        print("No data to generate workload CSVs.")
        return
    
    # Create output directory if it doesn't exist
    Path(output_dir).mkdir(parents=True, exist_ok=True)
    
    # Group by workload
    if 'workload' not in df.columns:
        print("Error: DataFrame does not contain 'workload' column.")
        return
    
    # For each workload, create a separate CSV
    for workload, group_df in df.groupby('workload'):
        output_file = Path(output_dir) / f"{workload}_results.csv"
        save_dataframe_with_ordered_columns(group_df, output_file)
    
    print(f"Generated {df['workload'].nunique()} workload-specific CSV files in {output_dir}")

def create_summary_df(df):
    """
    Create a summary DataFrame with aggregated statistics
    
    Args:
        df: Pandas DataFrame containing all results
        
    Returns:
        A pandas DataFrame containing the summary
    """
    if df.empty:
        print("No data to generate summary.")
        return pd.DataFrame()
    
    # Convert relevant columns to numeric
    numeric_columns = [
        'l1_tlb_hit_percentage', 'l2_tlb_hit_percentage', 'tlb_efficiency',
        'pgd_pwc_hit_percentage', 'pud_pwc_hit_percentage', 'pmd_pwc_hit_percentage',
        'page_table_memory_mb', 'pte_cache_hit_ratio',
        'total_access_cost_cycles', 'avg_memory_requests_per_translation'
    ]
    
    for col in numeric_columns:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')
    
    # Group by configuration parameters
    group_columns = [
        'experiment_name', 'workload',
        'pgd_size', 'pud_size', 'pmd_size', 'pte_size',
        'pgd_pwc_entries', 'pud_pwc_entries', 'pmd_pwc_entries',
        'toc_enabled', 'toc_size'
    ]
    
    # Only use group columns that exist in the DataFrame
    group_columns = [col for col in group_columns if col in df.columns]
    
    # Group and aggregate
    if not group_columns:
        print("Error: No grouping columns found in DataFrame.")
        return pd.DataFrame()
    
    # Calculate aggregations
    summary_df = df.groupby(group_columns).agg({
        col: 'mean' for col in numeric_columns if col in df.columns
    }).reset_index()
    
    # Add count of runs for each configuration
    count_df = df.groupby(group_columns).size().reset_index(name='run_count')
    summary_df = pd.merge(summary_df, count_df, on=group_columns)
    
    return summary_df

def create_toc_comparative_df(df):
    """
    Create a comparative DataFrame for TOC enabled vs disabled
    
    Args:
        df: Pandas DataFrame containing all results
        
    Returns:
        A pandas DataFrame with the comparison
    """
    if df.empty or 'toc_enabled' not in df.columns:
        print("Cannot create TOC comparative: DataFrame empty or missing 'toc_enabled' column.")
        return pd.DataFrame()
    
    # Convert TOC enabled to boolean
    df['toc_enabled'] = df['toc_enabled'].astype(str).str.lower() == 'true'
    
    # Convert relevant columns to numeric
    numeric_columns = [
        'l1_tlb_hit_percentage', 'l2_tlb_hit_percentage', 'tlb_efficiency',
        'pgd_pwc_hit_percentage', 'pud_pwc_hit_percentage', 'pmd_pwc_hit_percentage',
        'page_table_memory_mb', 'pte_cache_hit_ratio',
        'total_access_cost_cycles', 'avg_memory_requests_per_translation'
    ]
    
    for col in numeric_columns:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')
    
    # Group columns (configuration without TOC settings)
    group_columns = [
        'experiment_name', 'workload',
        'pgd_size', 'pud_size', 'pmd_size', 'pte_size',
        'pgd_pwc_entries', 'pud_pwc_entries', 'pmd_pwc_entries'
    ]
    
    # Only use group columns that exist in the DataFrame
    group_columns = [col for col in group_columns if col in df.columns]
    
    if not group_columns:
        print("Error: No grouping columns found in DataFrame.")
        return pd.DataFrame()
    
    # Split into TOC enabled and disabled
    enabled_df = df[df['toc_enabled'] == True].copy()
    disabled_df = df[df['toc_enabled'] == False].copy()
    
    # Rename columns for enabled
    enabled_columns = {col: f'toc_enabled_{col}' for col in numeric_columns if col in df.columns}
    enabled_df = enabled_df.groupby(group_columns).agg({
        col: 'mean' for col in numeric_columns if col in df.columns
    }).reset_index().rename(columns=enabled_columns)
    
    # Rename columns for disabled
    disabled_columns = {col: f'toc_disabled_{col}' for col in numeric_columns if col in df.columns}
    disabled_df = disabled_df.groupby(group_columns).agg({
        col: 'mean' for col in numeric_columns if col in df.columns
    }).reset_index().rename(columns=disabled_columns)
    
    # Merge the datasets
    comparative_df = pd.merge(enabled_df, disabled_df, on=group_columns, how='inner')
    
    # Calculate improvement percentages
    for col in numeric_columns:
        if col in df.columns:
            enabled_col = f'toc_enabled_{col}'
            disabled_col = f'toc_disabled_{col}'
            
            if enabled_col in comparative_df.columns and disabled_col in comparative_df.columns:
                # For cycle counts and memory requests, lower is better
                if col in ['total_access_cost_cycles', 'avg_memory_requests_per_translation']:
                    comparative_df[f'{col}_improvement'] = (
                        (comparative_df[disabled_col] - comparative_df[enabled_col]) / 
                        comparative_df[disabled_col] * 100
                    )
                else:
                    # For hit rates, higher is better
                    comparative_df[f'{col}_improvement'] = (
                        (comparative_df[enabled_col] - comparative_df[disabled_col]) / 
                        comparative_df[disabled_col] * 100
                    )
    
    return comparative_df

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='Gather results from memory simulator experiments')
    parser.add_argument('-d', '--base_dir', type=str, default='../experiment-results',
                        help='Base directory containing experiment results')
    parser.add_argument('-o', '--output', type=str, default='memory_sim_results.csv',
                        help='Output CSV file for all results')
    parser.add_argument('-s', '--summary', type=str, default='memory_sim_summary.csv',
                        help='Output CSV file for summary statistics')
    parser.add_argument('-w', '--workload_csvs', type=str, default='workload_results',
                        help='Directory for per-workload CSV files')
    parser.add_argument('-t', '--timestamp_csvs', type=str, default='timestamp_results',
                        help='Directory for per-timestamp CSV files')
    parser.add_argument('-c', '--comparative', type=str, default='toc_comparative.csv',
                        help='Output CSV file for TOC enabled vs disabled comparison')
    parser.add_argument('-a', '--all', action='store_true',
                        help='Generate all CSV outputs')
    
    args = parser.parse_args()
    
    # Gather results
    print(f"Gathering results from {args.base_dir}...")
    df = gather_results(args.base_dir)
    
    if df.empty:
        print("No results found. Exiting.")
        return
    
    print(f"Found {len(df)} experiment result files.")
    
    # Write full CSV
    if args.all or args.output:
        save_dataframe_with_ordered_columns(df, args.output)
    
    # Write per-timestamp CSVs
    if args.all or args.timestamp_csvs:
        create_timestamp_csvs(df, args.timestamp_csvs)
    
    # Write per-workload CSVs
    if args.all or args.workload_csvs:
        create_workload_csvs(df, args.workload_csvs)
    
    # Write summary CSV
    if args.all or args.summary:
        summary_df = create_summary_df(df)
        if not summary_df.empty:
            save_dataframe_with_ordered_columns(summary_df, args.summary)
    
    # Write comparative CSV
    if args.all or args.comparative:
        comparative_df = create_toc_comparative_df(df)
        if not comparative_df.empty:
            save_dataframe_with_ordered_columns(comparative_df, args.comparative)
    
    print("Done.")

if __name__ == "__main__":
    main()