
import os
import sys
import subprocess

DEFAULT_RESULTS_BASE_DIR = 'experiment-results'

SPECIAL_WORKLOAD_LIST = [
    # {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '40000']}, # ~19.6GB
    # {'name': 'BTree', 'options': ['3000000', '100000']}, #~300MB
    # {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-1']}, #~8.5GB
    # {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-1']}, #~8.4GB
    {'name': 'seq-list-static', 'options': ['-s', '24', '-e', '24', '-V']}, #~13.3GB
]

# other_options = ["-instr_threshold 20000000000"]
other_options = []

# a super huge workload list, only allowed to run with "-instr_threshold 2000000000" in other_options
SUPER_HUGE_WORKLOAD_LIST = [
    {'name': 'BTree', 'options': ['1500000000' '70000000']},
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '170000', '-p', '4000000']},
    {'name': 'gups-static', 'options': ['128']},
]

# Define workload lists
HUGE_WORKLOAD_LIST = [
    {'name': 'BTree', 'options': ['90000000', '100']}, # ~8.75GB
    {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/snb/social_network-sf10-numpart-1']}, #~27.9G
    {'name': 'gups-static', 'options': ['20']}, # ~16GB
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/snb/social_network-sf10-numpart-1']}, #~27.9G
    {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '40000']}, # ~19.6GB

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
    # {'name': 'BTree', 'options': ['4', '4']}, # ~71MB
    # {'name': 'xsbench-static', 'options': ['-t', '1', '-g', '2000', '-p', '40000']}, #~1.01GB
    # {'name': 'gups-static', 'options': ['10']}, # ~8.0G
    {'name': 'graphbig/dc', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-1']}, #~4.33GB
    {'name': 'graphbig/pr', 'options': ['--dataset', '~/workload/snb/social_network-sf3-numpart-1']}, #~4.26GB
]


def perf_test(workload_list, exp_dir, workload_base_dir):
    perf_base_cmd = (
        "sudo perf stat record "
        "-e dtlb_load_misses.walk_completed,"
        "dtlb_load_misses.walk_pending,"
        "dtlb_load_misses.walk_active,"
        "dtlb_store_misses.walk_completed,"
        "dtlb_store_misses.walk_pending,"
        "dtlb_store_misses.walk_active,"
        "itlb_misses.walk_completed,"
        "itlb_misses.walk_pending,"
        "itlb_misses.walk_active,"
        "cycles:ukhHG,"
        "task-clock:ukhHG,"
        "cpu-clock:ukhHG "
        "-I 5000 "
        "{executable} {options} "
        "2> {output_file}"
    )

    if not os.path.exists(exp_dir):
        os.makedirs(exp_dir)

    """Run performance tests on a list of workloads using perf"""
    # rerun 10 times
    # for i in range(10):
    for workload in workload_list:
        workload_path = os.path.join(workload_base_dir, workload['name'])
        if not os.path.exists(workload_path):
            print(f"Workload {workload['name']} does not exist at {workload_path}")
            continue
        
        output_dir = os.path.join(exp_dir, workload['name'])
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)

        output_file = f"{output_dir}/perf_output.txt"
        options = ' '.join(workload.get('options', []))
        
        run_cmd = perf_base_cmd.format(
            executable=workload_path,
            options=options,
            output_file=output_file
        )
        
        print(f"Running perf test for {workload['name']}")
        subprocess.run(run_cmd, shell=True, check=True)
        print(f"Completed perf test for {workload['name']}")





workload_list_type = "tiny"

if workload_list_type == 'large':
    workload_list = LARGE_WORKLOAD_LIST
elif workload_list_type == 'huge':
    workload_list = HUGE_WORKLOAD_LIST
elif workload_list_type == 'middle':
    workload_list = MIDDLE_WORKLOAD_LIST
elif workload_list_type == 'tiny':
    workload_list = TINY_WORKLOAD_LIST
elif workload_list_type == 'special':
    workload_list = SPECIAL_WORKLOAD_LIST
elif workload_list_type == 'super_huge':
    workload_list = SUPER_HUGE_WORKLOAD_LIST

if len(sys.argv) > 1:
    workload_list_type = sys.argv[1]
    if workload_list_type == 'large':
        workload_list = LARGE_WORKLOAD_LIST
    elif workload_list_type == 'huge':
        workload_list = HUGE_WORKLOAD_LIST
    elif workload_list_type == 'middle':
        workload_list = MIDDLE_WORKLOAD_LIST
    elif workload_list_type == 'tiny':
        workload_list = TINY_WORKLOAD_LIST
    elif workload_list_type == 'special':
        workload_list = SPECIAL_WORKLOAD_LIST
    elif workload_list_type == 'super_huge':
        workload_list = SUPER_HUGE_WORKLOAD_LIST
    else:
        print(f"Unknown workload list type: {workload_list_type}. Using default.")
        workload_list = TINY_WORKLOAD_LIST


print(f"Running performance tests for {workload_list_type}...")
perf_test(workload_list,  f"../perf-results/{workload_list_type}","/home/ym562/workload/")


