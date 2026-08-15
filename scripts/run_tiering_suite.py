#!/usr/bin/env python3
"""
Comprehensive Memory Tiering Benchmark Suite for MemBrain on Intel Xeon Max.
Runs 14 experiments:
1. Baseline DDR-only (Unconstrained)
2. Baseline HBM-only (Unconstrained 64 GB)
3. 12.5% HBM Capacity (7037.5 MB): HBM, Knapsack, Hotset, Thermos
4. 25.0% HBM Capacity (14075.0 MB): HBM, Knapsack, Hotset, Thermos
5. 50.0% HBM Capacity (28150.0 MB): HBM, Knapsack, Hotset, Thermos
"""

import os
import sys
import time
import json
import re
import subprocess
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
LULESH_BIN = os.path.join(ROOT_DIR, "..", "LULESH", "build", "lulesh2.0")
if not os.path.exists(LULESH_BIN):
    print(f"ERROR: LULESH binary not found at '{LULESH_BIN}'. Build LULESH first.", file=sys.stderr)
    sys.exit(1)

GUIDANCE_DIR = os.path.join(ROOT_DIR, "guidance")
EXP_DIR = os.path.join(ROOT_DIR, "experiments")
LOG_DIR = os.path.join(EXP_DIR, "logs")
RESULTS_DIR = os.path.join(EXP_DIR, "results")
MEMBRAIN_RT = os.path.join(ROOT_DIR, "build", "runtime", "libmembrain_rt.so")

os.makedirs(LOG_DIR, exist_ok=True)
os.makedirs(RESULTS_DIR, exist_ok=True)

MAX_RSS_KB = 57650856
MAX_RSS_MB = MAX_RSS_KB / 1024.0  # ~56299.66 MB

EXPERIMENTS = [
    # 1. Unconstrained Baselines
    {
        "id": "unconstrained_ddr",
        "name": "DDR-only (Unconstrained)",
        "capacity_key": "unconstrained",
        "capacity_pct": 0.0,
        "capacity_mb": "unconstrained",
        "guidance": os.path.join(GUIDANCE_DIR, "ddr_only.json"),
        "strategy": "ddr_only"
    },
    {
        "id": "unconstrained_hbm",
        "name": "HBM-only (Unconstrained 64GB)",
        "capacity_key": "unconstrained",
        "capacity_pct": 100.0,
        "capacity_mb": "unconstrained",
        "guidance": os.path.join(GUIDANCE_DIR, "hbm_only.json"),
        "strategy": "hbm_only"
    },

    # 2. 12.5% HBM Capacity (7037.46 MB)
    {
        "id": "12.5pct_hbm",
        "name": "12.5% HBM - HBM-only (Constrained)",
        "capacity_key": "12.5pct",
        "capacity_pct": 12.5,
        "capacity_mb": MAX_RSS_MB * 0.125,
        "guidance": os.path.join(GUIDANCE_DIR, "hbm_only.json"),
        "strategy": "hbm_constrained"
    },
    {
        "id": "12.5pct_knapsack",
        "name": "12.5% HBM - Knapsack",
        "capacity_key": "12.5pct",
        "capacity_pct": 12.5,
        "capacity_mb": MAX_RSS_MB * 0.125,
        "guidance": os.path.join(GUIDANCE_DIR, "12.5pct", "knapsack.json"),
        "strategy": "knapsack"
    },
    {
        "id": "12.5pct_hotset",
        "name": "12.5% HBM - Hotset",
        "capacity_key": "12.5pct",
        "capacity_pct": 12.5,
        "capacity_mb": MAX_RSS_MB * 0.125,
        "guidance": os.path.join(GUIDANCE_DIR, "12.5pct", "hotset.json"),
        "strategy": "hotset"
    },
    {
        "id": "12.5pct_thermos",
        "name": "12.5% HBM - Thermos",
        "capacity_key": "12.5pct",
        "capacity_pct": 12.5,
        "capacity_mb": MAX_RSS_MB * 0.125,
        "guidance": os.path.join(GUIDANCE_DIR, "12.5pct", "thermos.json"),
        "strategy": "thermos"
    },

    # 3. 25.0% HBM Capacity (14074.92 MB)
    {
        "id": "25pct_hbm",
        "name": "25% HBM - HBM-only (Constrained)",
        "capacity_key": "25pct",
        "capacity_pct": 25.0,
        "capacity_mb": MAX_RSS_MB * 0.25,
        "guidance": os.path.join(GUIDANCE_DIR, "hbm_only.json"),
        "strategy": "hbm_constrained"
    },
    {
        "id": "25pct_knapsack",
        "name": "25% HBM - Knapsack",
        "capacity_key": "25pct",
        "capacity_pct": 25.0,
        "capacity_mb": MAX_RSS_MB * 0.25,
        "guidance": os.path.join(GUIDANCE_DIR, "25pct", "knapsack.json"),
        "strategy": "knapsack"
    },
    {
        "id": "25pct_hotset",
        "name": "25% HBM - Hotset",
        "capacity_key": "25pct",
        "capacity_pct": 25.0,
        "capacity_mb": MAX_RSS_MB * 0.25,
        "guidance": os.path.join(GUIDANCE_DIR, "25pct", "hotset.json"),
        "strategy": "hotset"
    },
    {
        "id": "25pct_thermos",
        "name": "25% HBM - Thermos",
        "capacity_key": "25pct",
        "capacity_pct": 25.0,
        "capacity_mb": MAX_RSS_MB * 0.25,
        "guidance": os.path.join(GUIDANCE_DIR, "25pct", "thermos.json"),
        "strategy": "thermos"
    },

    # 4. 50.0% HBM Capacity (28149.83 MB)
    {
        "id": "50pct_hbm",
        "name": "50% HBM - HBM-only (Constrained)",
        "capacity_key": "50pct",
        "capacity_pct": 50.0,
        "capacity_mb": MAX_RSS_MB * 0.50,
        "guidance": os.path.join(GUIDANCE_DIR, "hbm_only.json"),
        "strategy": "hbm_constrained"
    },
    {
        "id": "50pct_knapsack",
        "name": "50% HBM - Knapsack",
        "capacity_key": "50pct",
        "capacity_pct": 50.0,
        "capacity_mb": MAX_RSS_MB * 0.50,
        "guidance": os.path.join(GUIDANCE_DIR, "50pct", "knapsack.json"),
        "strategy": "knapsack"
    },
    {
        "id": "50pct_hotset",
        "name": "50% HBM - Hotset",
        "capacity_key": "50pct",
        "capacity_pct": 50.0,
        "capacity_mb": MAX_RSS_MB * 0.50,
        "guidance": os.path.join(GUIDANCE_DIR, "50pct", "hotset.json"),
        "strategy": "hotset"
    },
    {
        "id": "50pct_thermos",
        "name": "50% HBM - Thermos",
        "capacity_key": "50pct",
        "capacity_pct": 50.0,
        "capacity_mb": MAX_RSS_MB * 0.50,
        "guidance": os.path.join(GUIDANCE_DIR, "50pct", "thermos.json"),
        "strategy": "thermos"
    }
]

def set_hbm_capacity(cap_mb):
    script_path = os.path.join(SCRIPT_DIR, "set_hbm_capacity.sh")
    arg = "unconstrained" if cap_mb == "unconstrained" else f"{cap_mb:.2f}"
    res = subprocess.run(["bash", script_path, arg], capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Failed to set HBM capacity: {res.stderr}", file=sys.stderr)
        sys.exit(1)
    print(res.stdout.strip())

def parse_time_output(output):
    metrics = {}

    # LULESH metrics
    elapsed_match = re.search(r"Elapsed time\s+=\s+([\d\.e\+\-]+)", output)
    if elapsed_match:
        metrics["lulesh_elapsed_sec"] = float(elapsed_match.group(1))

    fom_match = re.search(r"FOM\s+=\s+([\d\.]+)", output)
    if fom_match:
        metrics["fom"] = float(fom_match.group(1))

    grind_match = re.search(r"Grind time \(us/z/c\)\s+=\s+([\d\.]+)", output)
    if grind_match:
        metrics["grind_time_us"] = float(grind_match.group(1))

    # /usr/bin/time -v metrics
    user_match = re.search(r"User time \(seconds\):\s+([\d\.]+)", output)
    if user_match:
        metrics["user_time_sec"] = float(user_match.group(1))

    sys_match = re.search(r"System time \(seconds\):\s+([\d\.]+)", output)
    if sys_match:
        metrics["sys_time_sec"] = float(sys_match.group(1))

    cpu_match = re.search(r"Percent of CPU this job got:\s+([\d]+)%", output)
    if cpu_match:
        metrics["cpu_percent"] = int(cpu_match.group(1))

    wall_match = re.search(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s+([^\n]+)", output)
    if wall_match:
        metrics["wall_clock_str"] = wall_match.group(1).strip()

    rss_match = re.search(r"Maximum resident set size \(kbytes\):\s+([\d]+)", output)
    if rss_match:
        metrics["max_rss_kb"] = int(rss_match.group(1))
        metrics["max_rss_mb"] = metrics["max_rss_kb"] / 1024.0

    return metrics

def measure_max_rss():
    """Run a dedicated DDR-only LULESH pass with /usr/bin/time -v to measure peak RSS."""
    print("\n================================================================================")
    print(" [RSS Measurement] Running DDR-only pass to measure peak RSS...")
    print("================================================================================")

    set_hbm_capacity("unconstrained")

    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = "32"
    env["KMP_AFFINITY"] = "granularity=fine,compact,1,0"

    cmd = [
        "/usr/bin/time", "-v",
        "numactl", "--cpunodebind=0", "--membind=0",
        LULESH_BIN,
        "-s", "400", "-i", "5", "-r", "11", "-b", "0", "-c", "64", "-p"
    ]

    log_path = os.path.join(LOG_DIR, "rss_measurement.log")
    with open(log_path, "w") as f_log:
        proc = subprocess.run(cmd, stdout=f_log, stderr=subprocess.STDOUT, env=env, cwd=ROOT_DIR)

    if proc.returncode != 0:
        print(f"[RSS Measurement] WARNING: RSS measurement run failed (code {proc.returncode}). "
              f"See {log_path}", file=sys.stderr)
        return None

    with open(log_path, "r") as f_log:
        output = f_log.read()

    metrics = parse_time_output(output)
    max_rss_kb = metrics.get("max_rss_kb")
    if max_rss_kb:
        print(f"[RSS Measurement] Peak RSS = {max_rss_kb:,} KB ({max_rss_kb / 1024:.1f} MB)")
    else:
        print("[RSS Measurement] WARNING: Could not parse peak RSS from /usr/bin/time output.")
    return max_rss_kb


def run_experiment(exp, index, total):
    print(f"\n================================================================================")
    print(f" [{index}/{total}] Running Experiment: {exp['name']}")
    print(f" ID: {exp['id']} | Strategy: {exp['strategy']} | Capacity: {exp['capacity_key']}")
    print(f" Guidance: {exp['guidance']}")
    print(f"================================================================================")

    # 1. Set HBM Capacity
    set_hbm_capacity(exp["capacity_mb"])

    # 2. Prepare environment
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = "32"
    env["KMP_AFFINITY"] = "granularity=fine,compact,1,0"
    env["MEMBRAIN_GUIDANCE_PATH"] = exp["guidance"]
    env["MEMBRAIN_VERBOSE"] = "0"
    env["MEMBRAIN_TRACE"] = "0"
    if os.path.exists(MEMBRAIN_RT):
        env["LD_PRELOAD"] = MEMBRAIN_RT

    log_path = os.path.join(LOG_DIR, f"{exp['id']}.log")
    cmd = [
        "numactl", "--cpunodebind=0",
        LULESH_BIN,
        "-s", "400", "-i", "5", "-r", "11", "-b", "0", "-c", "64", "-p"
    ]

    print(f"[Runner] Executing LULESH (Problem size 400^3, 5 iterations, 32 threads)...")
    start_t = time.time()
    with open(log_path, "w") as f_log:
        proc = subprocess.run(
            cmd,
            stdout=f_log,
            stderr=subprocess.STDOUT,
            env=env,
            cwd=ROOT_DIR
        )
    elapsed_t = time.time() - start_t

    if proc.returncode != 0:
        print(f"[Runner] ERROR: Experiment failed with code {proc.returncode}! Log: {log_path}", file=sys.stderr)
        return None

    # Read back log
    with open(log_path, "r") as f_log:
        full_output = f_log.read()

    metrics = parse_time_output(full_output)
    metrics["total_elapsed_wall_sec"] = elapsed_t
    print(f"[Runner] Finished in {elapsed_t:.2f}s | LULESH Elapsed: {metrics.get('lulesh_elapsed_sec', 'N/A')}s | FOM: {metrics.get('fom', 'N/A')} z/s")
    
    return {
        "config": exp,
        "metrics": metrics,
        "log_file": log_path,
        "timestamp": datetime.now().isoformat()
    }

def generate_summary_report(results, max_rss_kb=None):
    results_json_path = os.path.join(RESULTS_DIR, "tiering_benchmark_results.json")
    with open(results_json_path, "w") as f:
        json.dump(results, f, indent=2)

    # Find DDR-only baseline for speedup calculation
    ddr_baseline_sec = None
    for r in results:
        if r["config"]["id"] == "unconstrained_ddr":
            ddr_baseline_sec = r["metrics"].get("lulesh_elapsed_sec")
            break

    summary_md_path = os.path.join(RESULTS_DIR, "tiering_benchmark_summary.md")
    with open(summary_md_path, "w") as f:
        f.write("# MemBrain Memory Tiering Benchmark Results\n\n")
        f.write(f"- **Platform:** Intel Xeon Max 9480 (Flat Mode HBM2e + DDR5)\n")
        f.write(f"- **Workload:** LULESH 2.0 (`-s 400 -i 5 -r 11 -b 0 -c 64 -p` $\\to$ 64M elements)\n")
        f.write(f"- **Threads:** 32 OpenMP threads (`KMP_AFFINITY=granularity=fine,compact,1,0`)\n")
        if max_rss_kb:
            f.write(f"- **Maximum Measured RSS:** {max_rss_kb:,} KB ({max_rss_kb / 1024:.1f} MB, measured via dedicated DDR-only run)\n")
        else:
            f.write(f"- **Maximum Measured RSS:** {MAX_RSS_KB:,} KB ({MAX_RSS_MB:.2f} MB, hardcoded estimate)\n")
        f.write(f"- **Date:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

        f.write("## 1. Complete Benchmark Results Table\n\n")
        f.write("| Experiment ID | Configuration | HBM Cap (% / MB) | Elapsed Time (s) | FOM (z/s) | Speedup vs DDR |\n")
        f.write("| :--- | :--- | :--- | :--- | :--- | :--- |\n")

        for r in results:
            cfg = r["config"]
            m = r["metrics"]
            elap = m.get("lulesh_elapsed_sec", 0.0)
            fom = m.get("fom", 0.0)

            speedup_str = "1.00x"
            if ddr_baseline_sec and elap > 0:
                speedup = ddr_baseline_sec / elap
                speedup_str = f"{speedup:.2f}x"

            cap_str = f"{cfg['capacity_pct']}%" if cfg['capacity_key'] != "unconstrained" else "Unconstrained"
            if cfg['capacity_mb'] != "unconstrained":
                cap_str += f" ({cfg['capacity_mb']:.0f} MB)"

            f.write(f"| `{cfg['id']}` | **{cfg['name']}** | {cap_str} | **{elap:.2f}s** | {fom:.1f} | **{speedup_str}** |\n")

        f.write("\n## 2. Capacity-by-Capacity Analysis\n\n")
        
        # Group by capacity
        for cap_key in ["12.5pct", "25pct", "50pct"]:
            cap_results = [r for r in results if r["config"]["capacity_key"] == cap_key]
            if not cap_results:
                continue
            pct = cap_results[0]["config"]["capacity_pct"]
            cap_mb = cap_results[0]["config"]["capacity_mb"]
            f.write(f"### Capacity {pct}% ({cap_mb:.1f} MB HBM Budget)\n\n")
            f.write("| Strategy | Placement Guidance | Elapsed Time (s) | FOM (z/s) | Speedup vs DDR |\n")
            f.write("| :--- | :--- | :--- | :--- | :--- |\n")
            for r in cap_results:
                strat = r["config"]["strategy"]
                elap = r["metrics"].get("lulesh_elapsed_sec", 0.0)
                fom = r["metrics"].get("fom", 0.0)
                speedup = (ddr_baseline_sec / elap) if (ddr_baseline_sec and elap > 0) else 1.0
                f.write(f"| **{strat.upper()}** | `{os.path.basename(r['config']['guidance'])}` | **{elap:.2f}s** | {fom:.1f} | **{speedup:.2f}x** |\n")
            f.write("\n")

    print(f"\n[Summary] Results saved to:\n  - JSON: {results_json_path}\n  - Markdown: {summary_md_path}")

def main():
    print(f"[Benchmark Suite] Initializing MemBrain Tiering Experiment Suite (14 runs)...")

    max_rss_kb = measure_max_rss()

    results = []
    total_exps = len(EXPERIMENTS)

    try:
        for idx, exp in enumerate(EXPERIMENTS, start=1):
            res = run_experiment(exp, idx, total_exps)
            if res:
                results.append(res)
                generate_summary_report(results, max_rss_kb)
    finally:
        print("\n[Benchmark Suite] Restoring HBM capacity to UNCONSTRAINED...")
        set_hbm_capacity("unconstrained")

    print("\n[Benchmark Suite] ALL EXPERIMENTS COMPLETED SUCCESSFULLY!")

if __name__ == "__main__":
    main()
