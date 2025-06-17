#!/usr/bin/env python3
"""
Standalone script to analyze existing perf results and generate CSV summaries
Usage: python analyze_perf_results.py [results_directory] [output_csv]
"""

import sys
import csv
from pathlib import Path


def parse_perf_output(file_path):
    """Parse perf output file and extract performance metrics"""
    metrics = {}
    
    try:
        with open(file_path, 'r') as f:
            lines = f.readlines()
        print(f"Parsing file: {file_path}")
        # Look for the Performance counter stats section
        in_stats_section = False
        
        for line in lines:
            # Skip empty lines
            if not line.strip():
                continue
                
            # Skip header/comment lines that start with #
            if line.strip().startswith('#'):
                continue
                
            # remove the content after the first '#'
            line = line.split('#')[0].strip()

            # Split the line by whitespace
            parts = line.split()
            
            # We need at least 3 parts: timestamp, value, metric_name
            if len(parts) >= 3:
                try:
                    # First part is timestamp (we can ignore for now)
                    # Second part is the count value
                    value_str = parts[1].replace(',', '')  # Remove any commas
                    
                    # Third part is the metric name
                    metric_name = parts[2]
                    
                    print(f"Parsed metric: {metric_name} = {value_str}")
                    
                    # Handle both integer and float values
                    # if metric_name not found, add as list
                    if metric_name not in metrics:
                        metrics[metric_name] = []
                    
                    if '.' in value_str:
                        metrics[metric_name].append(float(value_str))
                    else:
                        metrics[metric_name].append(int(value_str))
                        
                except (ValueError, IndexError):
                    # Skip lines that don't match expected format
                    continue
    
    except FileNotFoundError:
        print(f"Warning: Could not find file {file_path}")
        return None
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")
        return None
    
    return metrics


def calculate_derived_metrics(metrics):
    """Calculate walk_overhead and walkLatency from perf metrics"""
    try:
        # Extract required metrics with default values of 0
        dtlb_load_pending = metrics.get("dtlb_load_misses.walk_pending", 0)
        dtlb_store_pending = metrics.get("dtlb_store_misses.walk_pending", 0)
        itlb_pending = metrics.get("itlb_misses.walk_pending", 0)
        
        dtlb_load_completed = metrics.get("dtlb_load_misses.walk_completed", 0)
        dtlb_store_completed = metrics.get("dtlb_store_misses.walk_completed", 0)
        itlb_completed = metrics.get("itlb_misses.walk_completed", 0)
        
        cycles = metrics.get("cycles:ukhHG", 0)
        
        # Calculate walk_overhead
        total_pending = dtlb_load_pending + dtlb_store_pending + itlb_pending
        walk_overhead = total_pending / (cycles * 2) if cycles > 0 else 0
        
        # Calculate walkLatency
        total_completed = dtlb_load_completed + dtlb_store_completed + itlb_completed
        walk_latency = total_pending / total_completed if total_completed > 0 else 0
        
        return walk_overhead, walk_latency
    
    except Exception as e:
        print(f"Error calculating derived metrics: {e}")
        return 0, 0


def calculate_average_metrics(all_metrics):
    """Calculate average metrics from a list of metric dictionaries"""
    if not all_metrics:
        return {}
    
    # Get all metric names
    all_keys = set()
    for metrics in all_metrics:
        all_keys.update(metrics.keys())
    
    # print(all_metrics)
    # exit(0)
    # Calculate averages
    avg_metrics = {}
    for key in all_keys:
        values = all_metrics[0].get(key, [])
        if not values:
            continue
        avg_value = sum(values) / len(values)
        avg_metrics[key] = avg_value

    return avg_metrics


def generate_csv_summary(results_dir, output_csv="perf_summary.csv"):
    """Generate CSV summary from all perf output files in results directory"""
    
    results_path = Path(results_dir)
    if not results_path.exists():
        print(f"Results directory {results_dir} does not exist")
        return
    
    all_data = []
    
    # Group perf files by workload directory
    workload_dirs = {}
    
    # Find all perf_output files (both single and multiple runs)
    for perf_file in results_path.rglob("perf_output.txt"):
        workload_dir = perf_file.parent
        if workload_dir not in workload_dirs:
            workload_dirs[workload_dir] = []
        workload_dirs[workload_dir].append(perf_file)
    
    # Process each workload directory
    for workload_dir, perf_files in workload_dirs.items():
        # Get path relative to results directory and extract workload info
        relative_path = workload_dir.relative_to(results_path)
        path_parts = relative_path.parts
        
        if len(path_parts) >= 2:
            workload_type = path_parts[0]
            workload_name = '/'.join(path_parts[1:])  # Join remaining parts with '/'
        elif len(path_parts) == 1:
            workload_type = path_parts[0]
            workload_name = "unknown"
        else:
            workload_type = "unknown"
            workload_name = "unknown"
        
        print(f"Processing {workload_type}/{workload_name} ({len(perf_files)} files)")
        
        # Parse all perf files for this workload
        all_run_metrics = []
        for perf_file in sorted(perf_files):
            metrics = parse_perf_output(perf_file)
            if metrics is not None:
                all_run_metrics.append(metrics)
        
        if not all_run_metrics:
            print(f"  Warning: No valid metrics found for {workload_type}/{workload_name}")
            continue
        
        # Calculate average metrics
        avg_metrics = calculate_average_metrics(all_run_metrics)
        walk_overhead, walk_latency = calculate_derived_metrics(avg_metrics)
        
        # Create row data
        row_data = {
            'workload_type': workload_type,
            'workload_name': workload_name,
            'num_runs': len(all_run_metrics),
            'walk_overhead': walk_overhead,
            'walkLatency': walk_latency
        }
        
        # Add all averaged metrics
        row_data.update(avg_metrics)
        all_data.append(row_data)
    
    if not all_data:
        print("No data found to process")
        return
    
    # Get all unique column names
    all_columns = set()
    for row in all_data:
        all_columns.update(row.keys())
    
    # Define column order (put important ones first)
    priority_columns = ['workload_type', 'workload_name', 'num_runs', 'walk_overhead', 'walkLatency']
    other_columns = sorted([col for col in all_columns if col not in priority_columns])
    column_order = priority_columns + other_columns
    
    # Write CSV file
    if not output_csv.startswith('/'):
        csv_path = results_path / output_csv
    else:
        csv_path = Path(output_csv)
    
    with open(csv_path, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=column_order)
        writer.writeheader()
        writer.writerows(all_data)
    
    print(f"CSV summary written to {csv_path}")
    print(f"Processed {len(all_data)} workloads")
    
    # Print summary statistics
    if all_data:
        print("\n=== Summary Statistics ===")
        avg_walk_overhead = sum(row['walk_overhead'] for row in all_data) / len(all_data)
        avg_walk_latency = sum(row['walkLatency'] for row in all_data) / len(all_data)
        print(f"Average walk_overhead: {avg_walk_overhead:.6f}")
        print(f"Average walkLatency: {avg_walk_latency:.2f}")


def main():
    """Main function to handle command line arguments"""
    if len(sys.argv) < 2:
        print("Usage: python analyze_perf_results.py <results_directory> [output_csv]")
        print("Example: python analyze_perf_results.py ../perf-results perf_analysis.csv")
        sys.exit(1)
    
    results_dir = sys.argv[1]
    output_csv = sys.argv[2] if len(sys.argv) > 2 else "perf_summary.csv"
    
    print(f"Analyzing perf results in: {results_dir}")
    print(f"Output CSV: {output_csv}")
    
    generate_csv_summary(results_dir, output_csv)


if __name__ == "__main__":
    main()