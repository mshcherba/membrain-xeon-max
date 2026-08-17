#!/usr/bin/env python3
"""
MemBrain Generic Multi-Benchmark Memory Tiering Suite Runner.
Executes memory tiering experiments across Intel Xeon Max (HBM2e + DDR5).

Execution Suite (14 configurations):
  1. DDR-only (Unconstrained, strictly bound to DDR5 NUMA node 0)
  2. HBM-only (Unconstrained 64 GB, NUMA node 2) -> Reference Baseline
  3. For capacity in (12.5%, 25.0%, 50.0% of peak RSS):
       For strategy in (first-touch, knapsack, hotset, thermos):
           Run (capacity, strategy)

Performs N configurable repetitions (default: 5), calculates comprehensive FOM
statistics (Mean, Min, Max, StdDev, StdErr, 95% CI margin of error, Relative FOM vs HBM baseline),
and exports Markdown and JSON reports.
"""

import os
import sys
import time
import json
import re
import math
import argparse
import subprocess
from datetime import datetime

import statistics
import scipy.stats as stats

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MEMBRAIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
BENCHMARKS_DIR = os.path.join(MEMBRAIN_ROOT, "benchmarks")
MEMBRAIN_RT = os.path.join(MEMBRAIN_ROOT, "build", "runtime", "libmembrain_rt.so")
CAPACITY_SCRIPT = os.path.join(MEMBRAIN_ROOT, "scripts", "set_hbm_capacity.sh")
OPTIMIZER_SCRIPT = os.path.join(MEMBRAIN_ROOT, "optimizer", "membrain_opt.py")


def compute_statistics(values, confidence=0.95):
    """
    Compute statistical metrics using standard library statistics and scipy.stats:
    Mean, Min, Max, Median, Sample Standard Deviation, Standard Error of the Mean,
    and two-tailed Student's t Confidence Interval.
    """
    n = len(values)
    if n == 0:
        raise ValueError("Cannot compute statistics for empty values list")

    mean_val = statistics.mean(values)
    min_val = min(values)
    max_val = max(values)
    median_val = statistics.median(values)

    if n > 1:
        stdev = statistics.stdev(values)
        stderr = float(stats.sem(values))
        ci_lower, ci_upper = stats.t.interval(confidence, df=n - 1, loc=mean_val, scale=stderr)
        ci_lower = float(ci_lower)
        ci_upper = float(ci_upper)
        ci_margin = ci_upper - mean_val
        ci_margin_pct = (ci_margin / mean_val * 100.0) if mean_val != 0 else 0.0
    else:
        stdev = 0.0
        stderr = 0.0
        ci_margin = 0.0
        ci_margin_pct = 0.0
        ci_lower = mean_val
        ci_upper = mean_val

    return {
        "n": n,
        "values": values,
        "mean": mean_val,
        "min": min_val,
        "max": max_val,
        "median": median_val,
        "stdev": stdev,
        "stderr": stderr,
        "ci_margin": ci_margin,
        "ci_margin_pct": ci_margin_pct,
        "ci_lower": ci_lower,
        "ci_upper": ci_upper
    }


def set_hbm_capacity(cap_mb, dry_run=False):
    """Configure HBM capacity via set_hbm_capacity.sh hugepages reservation."""
    if dry_run:
        print(f"[DRY-RUN] set_hbm_capacity.sh -> {cap_mb}")
        return

    arg = "unconstrained" if cap_mb == "unconstrained" else f"{cap_mb:.2f}"
    res = subprocess.run(["bash", CAPACITY_SCRIPT, arg], capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Failed to set HBM capacity ({arg}): {res.stderr}")
    if res.stdout.strip():
        print(res.stdout.strip())


class BenchmarkSuiteRunner:
    def __init__(self, benchmark_name, repeats=5, custom_args=None,
                 capacities=None, strategies=None, dry_run=False, skip_rss=False):
        self.benchmark_name = benchmark_name
        self.repeats = repeats
        self.custom_args = custom_args
        self.capacities = capacities or [12.5, 25.0, 50.0]
        self.strategies = strategies or ["first-touch", "knapsack", "hotset", "thermos"]
        self.dry_run = dry_run
        self.skip_rss = skip_rss

        self.bench_dir = os.path.join(BENCHMARKS_DIR, benchmark_name)
        if not os.path.exists(self.bench_dir):
            raise FileNotFoundError(f"Benchmark directory not found: {self.bench_dir}")

        self.config_path = os.path.join(self.bench_dir, "config.json")
        if not os.path.exists(self.config_path):
            raise FileNotFoundError(f"Benchmark config not found: {self.config_path}")

        with open(self.config_path, "r") as f:
            self.config = json.load(f)

        # Resolve binary path
        raw_bin = self.config.get("binary_path", "")
        self.binary_path = os.path.abspath(os.path.join(self.bench_dir, raw_bin)) if not os.path.isabs(raw_bin) else raw_bin

        # Working and output directories
        self.guidance_dir = os.path.join(self.bench_dir, "guidance")
        self.exp_dir = os.path.join(self.bench_dir, "experiments")
        self.log_dir = os.path.join(self.exp_dir, "logs")
        self.results_dir = os.path.join(self.exp_dir, "results")

        os.makedirs(self.guidance_dir, exist_ok=True)
        os.makedirs(self.log_dir, exist_ok=True)
        os.makedirs(self.results_dir, exist_ok=True)

        self.fom_unit = self.config.get("fom_unit", "FOM")
        self.cli_args = self.custom_args if self.custom_args is not None else self.config.get("default_args", [])

    def parse_metrics(self, output):
        """Extract FOM, elapsed time, grind time, max RSS using benchmark config regex patterns."""
        metrics = {}
        patterns = self.config.get("patterns", {})

        # Primary FOM metric
        fom_pat = patterns.get("fom", r"FOM\s*=\s*([\d\.]+)")
        fom_m = re.search(fom_pat, output, re.IGNORECASE)
        if fom_m:
            metrics["fom"] = float(fom_m.group(1))

        # Elapsed execution time (auxiliary)
        elapsed_pat = patterns.get("elapsed_sec", r"Elapsed time\s*=\s*([\d\.e\+\-]+)")
        el_m = re.search(elapsed_pat, output, re.IGNORECASE)
        if el_m:
            metrics["elapsed_sec"] = float(el_m.group(1))

        # Grind time if available
        grind_pat = patterns.get("grind_time_us", r"Grind time.*=\s*([\d\.]+)")
        gr_m = re.search(grind_pat, output, re.IGNORECASE)
        if gr_m:
            metrics["grind_time_us"] = float(gr_m.group(1))

        # Peak RSS from /usr/bin/time -v
        rss_m = re.search(r"Maximum resident set size \(kbytes\):\s*([\d]+)", output, re.IGNORECASE)
        if rss_m:
            metrics["max_rss_kb"] = int(rss_m.group(1))
            metrics["max_rss_mb"] = metrics["max_rss_kb"] / 1024.0

        # Wall clock from /usr/bin/time -v
        wall_m = re.search(r"Elapsed \(wall clock\) time.*:\s*([^\n]+)", output, re.IGNORECASE)
        if wall_m:
            metrics["wall_clock_str"] = wall_m.group(1).strip()

        return metrics

    def measure_peak_rss(self):
        """
        Execute a dedicated, separate HBM-only run wrapped with /usr/bin/time -v
        prior to guidance generation to accurately extract peak RSS.
        """
        print("\n" + "=" * 80)
        print(" [Phase 1: Dedicated Peak RSS Measurement]")
        print(" Running HBM-only pass with /usr/bin/time -v to measure Maximum Resident Set Size...")
        print("=" * 80)

        if self.skip_rss:
            peak_kb = self.config.get("default_max_rss_kb")
            if not peak_kb:
                raise ValueError("default_max_rss_kb not defined in config.json while --skip-rss was requested.")
            print(f"[RSS Measurement] Skipped measurement via flag. Using config RSS: {peak_kb:,} KB ({peak_kb / 1024:.1f} MB)")
            return peak_kb

        if self.dry_run:
            print("[DRY-RUN] Measuring peak RSS on HBM-only pass...")
            return self.config.get("default_max_rss_kb", 57650856)

        set_hbm_capacity("unconstrained", dry_run=False)

        env = os.environ.copy()
        for k, v in self.config.get("environment", {}).items():
            env[k] = str(v)

        cmd = [
            "/usr/bin/time", "-v",
            "numactl", "--cpunodebind=0", "--membind=2",
            self.binary_path
        ] + self.cli_args

        log_path = os.path.join(self.log_dir, "rss_measurement_hbm_only.log")
        print(f"[RSS Measurement] Executing: {' '.join(cmd)}")
        with open(log_path, "w") as f_log:
            proc = subprocess.run(cmd, stdout=f_log, stderr=subprocess.STDOUT, env=env, cwd=MEMBRAIN_ROOT)

        if proc.returncode != 0:
            raise RuntimeError(f"Peak RSS measurement run failed (exit code {proc.returncode}). Log: {log_path}")

        with open(log_path, "r") as f_log:
            output = f_log.read()

        metrics = self.parse_metrics(output)
        peak_kb = metrics.get("max_rss_kb")
        if not peak_kb:
            raise RuntimeError(f"Could not parse 'Maximum resident set size' from output. Log: {log_path}")

        print(f"[RSS Measurement] Peak RSS = {peak_kb:,} KB ({peak_kb / 1024:.1f} MB)")
        return peak_kb

    def generate_guidance_files(self, peak_rss_kb):
        """
        Generate guidance files:
          - hbm_only.json (all sites tier 2; also used for first-touch)
          - for each capacity: knapsack.json, hotset.json, thermos.json
        """
        print("\n" + "=" * 80)
        print(" [Phase 2: Guidance File Generation]")
        print("=" * 80)

        if self.dry_run:
            print("[DRY-RUN] Skipping physical guidance file creation in dry-run mode.")
            return

        peak_rss_mb = peak_rss_kb / 1024.0

        # Find allocation sites or profile data
        profile_path = os.path.join(self.bench_dir, "profile_data.json")
        sites_path = os.path.join(self.bench_dir, "allocation_sites.json")
        if not os.path.exists(sites_path):
            raw_sites = self.config.get("allocation_sites_path")
            if raw_sites:
                sites_path = os.path.abspath(os.path.join(self.bench_dir, raw_sites))

        # 1. Generate hbm_only.json
        sites_for_static = []
        if os.path.exists(profile_path):
            with open(profile_path, "r") as f:
                sites_for_static = json.load(f)
        elif os.path.exists(sites_path):
            with open(sites_path, "r") as f:
                sites_for_static = json.load(f)
        else:
            raise FileNotFoundError(f"Neither profile_data.json nor allocation_sites.json found for {self.benchmark_name}")

        hbm_guidance = [{"site_id": s["site_id"], "tier": 2} for s in sites_for_static]
        hbm_only_path = os.path.join(self.guidance_dir, "hbm_only.json")
        with open(hbm_only_path, "w") as f:
            json.dump(hbm_guidance, f, indent=2)
        print(f"[Guidance] Saved HBM-only guidance -> '{hbm_only_path}'")

        # 2. Generate Optimizer Guidances (knapsack, hotset, thermos) for each capacity
        sys.path.insert(0, os.path.join(MEMBRAIN_ROOT, "optimizer"))
        from membrain_opt import (
            load_profile_data,
            run_knapsack_optimization,
            run_hotset_optimization,
            run_thermos_optimization,
            save_guidance_json
        )

        if not os.path.exists(profile_path):
            raise FileNotFoundError(f"profile_data.json not found at '{profile_path}'. Run profile.sh first.")

        profile_sites = load_profile_data(profile_path)
        for pct in self.capacities:
            cap_mb = peak_rss_mb * (pct / 100.0)
            cap_bytes = int(cap_mb * 1024 * 1024)
            cap_key = f"{pct:g}pct"
            cap_dir = os.path.join(self.guidance_dir, cap_key)
            os.makedirs(cap_dir, exist_ok=True)

            print(f"[Guidance] Generating for Capacity {pct}% ({cap_mb:.1f} MB)...")

            # Knapsack
            knap_g, _ = run_knapsack_optimization(profile_sites, cap_bytes)
            save_guidance_json(knap_g, os.path.join(cap_dir, "knapsack.json"))

            # Hotset
            hot_g, _ = run_hotset_optimization(profile_sites, cap_bytes)
            save_guidance_json(hot_g, os.path.join(cap_dir, "hotset.json"))

            # Thermos
            therm_g, _ = run_thermos_optimization(profile_sites, cap_bytes)
            save_guidance_json(therm_g, os.path.join(cap_dir, "thermos.json"))

    def build_experiments(self, peak_rss_kb):
        """Construct the 14 experiment configurations according to user specifications."""
        peak_rss_mb = peak_rss_kb / 1024.0
        hbm_only_guidance = os.path.join(self.guidance_dir, "hbm_only.json")

        experiments = []

        # 1. Unconstrained DDR-only
        experiments.append({
            "id": "unconstrained_ddr",
            "name": "DDR-only (Unconstrained)",
            "capacity_key": "unconstrained",
            "capacity_pct": 0.0,
            "capacity_mb": "unconstrained",
            "strategy": "ddr_only",
            "numa_mode": "membind_0",
            "use_runtime": False,
            "guidance_file": None,
            "is_baseline": False
        })

        # 2. Unconstrained HBM-only (Baseline)
        experiments.append({
            "id": "unconstrained_hbm",
            "name": "HBM-only (Unconstrained 64GB)",
            "capacity_key": "unconstrained",
            "capacity_pct": 100.0,
            "capacity_mb": "unconstrained",
            "strategy": "hbm_only",
            "numa_mode": "membind_2",
            "use_runtime": True if os.path.exists(MEMBRAIN_RT) else False,
            "guidance_file": hbm_only_guidance if os.path.exists(hbm_only_guidance) else None,
            "is_baseline": True
        })

        # 3. Constrained Capacity Tiers
        for pct in self.capacities:
            cap_key = f"{pct:g}pct"
            cap_mb = peak_rss_mb * (pct / 100.0)
            cap_dir = os.path.join(self.guidance_dir, cap_key)

            for strat in self.strategies:
                strat_clean = strat.lower().replace("_", "-")
                
                # First-touch reuses hbm_only.json guidance under constrained capacity
                if strat_clean in ("first-touch", "first_touch", "hbm-constrained", "hbm"):
                    guidance = hbm_only_guidance if os.path.exists(hbm_only_guidance) else None
                    strategy_id = "first-touch"
                    strat_name = f"{pct:g}% HBM - First-Touch"
                elif strat_clean == "knapsack":
                    guidance = os.path.join(cap_dir, "knapsack.json")
                    strategy_id = "knapsack"
                    strat_name = f"{pct:g}% HBM - Knapsack"
                elif strat_clean == "hotset":
                    guidance = os.path.join(cap_dir, "hotset.json")
                    strategy_id = "hotset"
                    strat_name = f"{pct:g}% HBM - Hotset"
                elif strat_clean == "thermos":
                    guidance = os.path.join(cap_dir, "thermos.json")
                    strategy_id = "thermos"
                    strat_name = f"{pct:g}% HBM - Thermos"
                else:
                    guidance = os.path.join(cap_dir, f"{strat_clean}.json")
                    strategy_id = strat_clean
                    strat_name = f"{pct:g}% HBM - {strat.capitalize()}"

                experiments.append({
                    "id": f"{cap_key}_{strategy_id.replace('-', '_')}",
                    "name": strat_name,
                    "capacity_key": cap_key,
                    "capacity_pct": pct,
                    "capacity_mb": cap_mb,
                    "strategy": strategy_id,
                    "numa_mode": "preferred_2",
                    "use_runtime": True if os.path.exists(MEMBRAIN_RT) else False,
                    "guidance_file": guidance,
                    "is_baseline": False
                })

        return experiments

    def run_single_iteration(self, exp, run_idx, total_runs):
        """Execute a single repetition of an experiment configuration."""
        env = os.environ.copy()
        for k, v in self.config.get("environment", {}).items():
            env[k] = str(v)

        if exp["use_runtime"] and os.path.exists(MEMBRAIN_RT):
            env["LD_PRELOAD"] = MEMBRAIN_RT
            env["MEMBRAIN_VERBOSE"] = "0"
            env["MEMBRAIN_TRACE"] = "0"
            if exp["guidance_file"] and os.path.exists(exp["guidance_file"]):
                env["MEMBRAIN_GUIDANCE_PATH"] = exp["guidance_file"]
        else:
            env.pop("LD_PRELOAD", None)
            env.pop("MEMBRAIN_GUIDANCE_PATH", None)

        numactl_cmd = ["numactl", "--cpunodebind=0"]
        if exp["numa_mode"] == "membind_0":
            numactl_cmd.append("--membind=0")
        elif exp["numa_mode"] == "membind_2":
            numactl_cmd.append("--membind=2")
        else:
            numactl_cmd.append("--preferred=2")

        cmd = numactl_cmd + [self.binary_path] + self.cli_args
        log_file = os.path.join(self.log_dir, f"{exp['id']}_run{run_idx}.log")

        if self.dry_run:
            print(f"  [DRY-RUN] Repetition {run_idx}/{total_runs}: {' '.join(cmd)}")
            return {"fom": 1000.0, "elapsed_sec": 10.0, "log_file": log_file}

        start_t = time.time()
        with open(log_file, "w") as f_log:
            proc = subprocess.run(cmd, stdout=f_log, stderr=subprocess.STDOUT, env=env, cwd=MEMBRAIN_ROOT)
        wall_time = time.time() - start_t

        if proc.returncode != 0:
            raise RuntimeError(f"Repetition {run_idx} of {exp['id']} failed with exit code {proc.returncode}. Check log: {log_file}")

        with open(log_file, "r") as f_log:
            output = f_log.read()

        parsed = self.parse_metrics(output)
        if "fom" not in parsed:
            raise RuntimeError(f"Could not parse FOM from output of {exp['id']} (run {run_idx}). Check log: {log_file}")

        if "elapsed_sec" not in parsed:
            parsed["elapsed_sec"] = wall_time
        parsed["log_file"] = log_file

        fom_str = f"{parsed['fom']:.2f} {self.fom_unit}"
        print(f"  -> Repetition {run_idx}/{total_runs}: FOM = {fom_str} | Elapsed = {parsed.get('elapsed_sec', 0.0):.2f}s")
        return parsed

    def run_suite(self):
        """Execute complete 14-configuration benchmark suite with repetitions."""
        if not os.path.exists(self.binary_path):
            if not self.dry_run:
                raise FileNotFoundError(f"Benchmark binary not found at '{self.binary_path}'. Build the benchmark first.")

        print("=" * 80)
        print(f" MemBrain Benchmark Suite: {self.config.get('name', self.benchmark_name)}")
        print(f" Target Binary: {self.binary_path}")
        print(f" Repetitions:   {self.repeats} per configuration")
        print(f" CLI Arguments: {' '.join(self.cli_args)}")
        print("=" * 80)

        # 1. Measure Peak RSS via dedicated HBM-only run
        peak_rss_kb = self.measure_peak_rss()

        # 2. Generate guidance files
        self.generate_guidance_files(peak_rss_kb)

        # 3. Build experiment matrix
        experiments = self.build_experiments(peak_rss_kb)
        total_configs = len(experiments)

        print("\n" + "=" * 80)
        print(f" [Phase 3: Benchmark Suite Execution ({total_configs} Configurations x {self.repeats} Runs)]")
        print("=" * 80)

        results = []
        current_cap_mb = None

        try:
            for cfg_idx, exp in enumerate(experiments, start=1):
                print(f"\n[{cfg_idx}/{total_configs}] Config: {exp['name']} (ID: {exp['id']})")
                print(f"     Capacity: {exp['capacity_key']} | Strategy: {exp['strategy']} | NUMA: {exp['numa_mode']}")
                if exp["guidance_file"]:
                    print(f"     Guidance: {os.path.basename(exp['guidance_file'])}")

                # Adjust HBM capacity only when it changes
                target_cap = exp["capacity_mb"]
                if target_cap != current_cap_mb:
                    set_hbm_capacity(target_cap, dry_run=self.dry_run)
                    current_cap_mb = target_cap

                fom_runs = []
                time_runs = []
                run_logs = []

                for r in range(1, self.repeats + 1):
                    run_res = self.run_single_iteration(exp, r, self.repeats)
                    if run_res:
                        if "fom" in run_res:
                            fom_runs.append(run_res["fom"])
                        if "elapsed_sec" in run_res:
                            time_runs.append(run_res["elapsed_sec"])
                        run_logs.append(run_res["log_file"])

                fom_stats = compute_statistics(fom_runs)
                time_stats = compute_statistics(time_runs)

                exp_result = {
                    "config": exp,
                    "fom_stats": fom_stats,
                    "time_stats": time_stats,
                    "raw_runs": {
                        "fom": fom_runs,
                        "elapsed_sec": time_runs,
                        "logs": run_logs
                    },
                    "timestamp": datetime.now().isoformat()
                }
                results.append(exp_result)

        finally:
            print("\n[Cleanup] Restoring HBM capacity to UNCONSTRAINED...")
            set_hbm_capacity("unconstrained", dry_run=self.dry_run)

        # 4. Statistical Post-Processing & Report Generation
        self.generate_reports(results, peak_rss_kb)

    def generate_reports(self, results, peak_rss_kb):
        """Compute relative baseline metrics and export Markdown & JSON summaries."""
        # Find HBM-only baseline
        hbm_baseline_fom = None
        for r in results:
            if r["config"].get("is_baseline", False) or r["config"]["id"] == "unconstrained_hbm":
                hbm_baseline_fom = r["fom_stats"]["mean"]
                break

        # Compute relative FOM and speedup for all configurations
        for r in results:
            mean_fom = r["fom_stats"]["mean"]
            if hbm_baseline_fom and hbm_baseline_fom > 0 and mean_fom > 0:
                rel_pct = (mean_fom / hbm_baseline_fom) * 100.0
                speedup_factor = mean_fom / hbm_baseline_fom
            else:
                rel_pct = 100.0 if r["config"].get("is_baseline") else 0.0
                speedup_factor = 1.0 if r["config"].get("is_baseline") else 0.0

            r["relative_to_hbm_baseline"] = {
                "relative_fom_pct": rel_pct,
                "speedup_factor": speedup_factor
            }

        # Save JSON results
        json_path = os.path.join(self.results_dir, "benchmark_results.json")
        with open(json_path, "w") as f:
            json.dump({
                "benchmark": self.benchmark_name,
                "date": datetime.now().isoformat(),
                "peak_rss_kb": peak_rss_kb,
                "repeats": self.repeats,
                "hbm_baseline_fom_mean": hbm_baseline_fom,
                "fom_unit": self.fom_unit,
                "results": results
            }, f, indent=2)

        # Save Markdown Summary
        md_path = os.path.join(self.results_dir, "benchmark_summary.md")
        with open(md_path, "w") as f:
            f.write(f"# MemBrain Benchmark Results: {self.config.get('name', self.benchmark_name)}\n\n")
            f.write(f"- **Platform:** Intel Xeon Max (Flat Mode HBM2e + DDR5)\n")
            f.write(f"- **Workload Arguments:** `{' '.join(self.cli_args)}`\n")
            f.write(f"- **Repetitions:** {self.repeats} runs per configuration (Mean $\\pm$ 95% Confidence Interval)\n")
            f.write(f"- **Peak Measured RSS:** {peak_rss_kb:,} KB ({peak_rss_kb / 1024:.1f} MB)\n")
            f.write(f"- **Primary Metric:** FOM ({self.fom_unit})\n")
            f.write(f"- **Reference Baseline:** **HBM-only (Unconstrained)** ($100.0\\%$, $1.00\\times$)\n")
            f.write(f"- **Date:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

            f.write("## 1. Overall FOM & Performance Comparison\n\n")
            f.write(f"| Configuration | HBM Cap (% / MB) | Mean FOM ({self.fom_unit}) | StdDev | 95% Margin of Error | Relative FOM vs HBM | Mean Time (s) |\n")
            f.write("| :--- | :--- | :--- | :--- | :--- | :--- | :--- |\n")

            for r in results:
                cfg = r["config"]
                fs = r["fom_stats"]
                ts = r["time_stats"]
                rel = r["relative_to_hbm_baseline"]

                cap_str = f"{cfg['capacity_pct']}%" if cfg['capacity_key'] != "unconstrained" else "Unconstrained"
                if cfg['capacity_mb'] != "unconstrained":
                    cap_str += f" ({cfg['capacity_mb']:.0f} MB)"

                mean_fom_str = f"**{fs['mean']:.2f}**"
                std_str = f"±{fs['stdev']:.2f}" if fs['n'] > 1 else "-"
                moe_str = f"±{fs['ci_margin']:.2f} (±{fs['ci_margin_pct']:.1f}%)" if fs['n'] > 1 else "-"
                rel_fom_str = f"**{rel['relative_fom_pct']:.1f}%** ({rel['speedup_factor']:.2f}x)" if hbm_baseline_fom else "-"
                mean_time_str = f"{ts['mean']:.2f}s" if ts['mean'] > 0 else "-"

                f.write(f"| **{cfg['name']}** | {cap_str} | {mean_fom_str} | {std_str} | {moe_str} | {rel_fom_str} | {mean_time_str} |\n")

            f.write("\n## 2. Capacity-by-Capacity Breakdown\n\n")
            for pct in self.capacities:
                cap_key = f"{pct:g}pct"
                cap_res = [r for r in results if r["config"]["capacity_key"] == cap_key]
                if not cap_res:
                    continue
                cap_mb = cap_res[0]["config"]["capacity_mb"]
                f.write(f"### Capacity Tier {pct}% ({cap_mb:.1f} MB HBM Budget)\n\n")
                f.write(f"| Strategy | Mean FOM ({self.fom_unit}) | 95% Confidence Interval | Relative FOM vs HBM | Mean Time (s) |\n")
                f.write("| :--- | :--- | :--- | :--- | :--- |\n")

                for r in cap_res:
                    fs = r["fom_stats"]
                    ts = r["time_stats"]
                    rel = r["relative_to_hbm_baseline"]
                    strat_name = r["config"]["strategy"].upper()
                    ci_str = f"[{fs['ci_lower']:.2f}, {fs['ci_upper']:.2f}]" if fs['n'] > 1 else f"{fs['mean']:.2f}"
                    rel_str = f"**{rel['relative_fom_pct']:.1f}%** ({rel['speedup_factor']:.2f}x)"

                    f.write(f"| **{strat_name}** | **{fs['mean']:.2f}** | {ci_str} | {rel_str} | {ts['mean']:.2f}s |\n")
                f.write("\n")

        # Terminal Summary Table
        print("\n" + "=" * 100)
        print(" MEMBRAIN BENCHMARK RESULTS SUMMARY (Baseline: HBM-only)")
        print("=" * 100)
        print(f"{'Configuration':<35} | {'Mean FOM (' + self.fom_unit + ')':<18} | {'95% MoE':<16} | {'Rel vs HBM':<12} | {'Mean Time':<10}")
        print("-" * 100)
        for r in results:
            cfg = r["config"]
            fs = r["fom_stats"]
            ts = r["time_stats"]
            rel = r["relative_to_hbm_baseline"]
            moe = f"±{fs['ci_margin_pct']:.1f}%" if fs['n'] > 1 else "-"
            rel_str = f"{rel['relative_fom_pct']:.1f}% ({rel['speedup_factor']:.2f}x)" if hbm_baseline_fom else "-"
            print(f"{cfg['name']:<35} | {fs['mean']:>14.2f}     | {moe:>14} | {rel_str:>12} | {ts['mean']:>8.2f}s")
        print("=" * 100)
        print(f"\n[Summary] Results successfully written to:\n  - JSON:     {json_path}\n  - Markdown: {md_path}\n")


def main():
    parser = argparse.ArgumentParser(description="MemBrain Generic Multi-Benchmark Memory Tiering Suite Runner")
    parser.add_argument("--benchmark", "-b", default="lulesh", help="Benchmark name in benchmarks/<name> (default: lulesh)")
    parser.add_argument("--repeats", "-r", type=int, default=5, help="Number of repetitions per configuration (default: 5)")
    parser.add_argument("--capacities", "-c", nargs="+", type=float, default=[12.5, 25.0, 50.0], help="HBM capacity percentages (default: 12.5 25.0 50.0)")
    parser.add_argument("--strategies", "-s", nargs="+", default=["first-touch", "knapsack", "hotset", "thermos"], help="Strategies to run (default: first-touch knapsack hotset thermos)")
    parser.add_argument("--skip-rss", action="store_true", help="Skip dedicated RSS measurement run and use default from config")
    parser.add_argument("--dry-run", action="store_true", help="Print experiment execution commands without running them")
    parser.add_argument("bench_args", nargs="*", help="Optional override arguments passed directly to the benchmark binary")

    args = parser.parse_args()
    custom_args = args.bench_args if len(args.bench_args) > 0 else None

    runner = BenchmarkSuiteRunner(
        benchmark_name=args.benchmark,
        repeats=args.repeats,
        custom_args=custom_args,
        capacities=args.capacities,
        strategies=args.strategies,
        dry_run=args.dry_run,
        skip_rss=args.skip_rss
    )
    runner.run_suite()


if __name__ == "__main__":
    main()
