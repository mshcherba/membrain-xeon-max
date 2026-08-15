#!/usr/bin/env python3
"""
Generate All Site Tier Guidance Configurations for MemBrain Memory Tiering Experiments.
Capacities: 12.5%, 25%, 50% of Maximum RSS (57,650,856 KB -> 56,300 MB).
Algorithms: Knapsack, Hotset, Thermos.
Baselines: DDR-only (Tier 0), HBM-only (Tier 2).
"""

import os
import sys
import json
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
PROFILE_PATH = os.path.join(ROOT_DIR, "profile_data.json")
SITES_PATH = os.path.join(ROOT_DIR, "LULESH", "build", "allocation_sites.json")
GUIDANCE_DIR = os.path.join(ROOT_DIR, "guidance")

MAX_RSS_KB = 57650856
MAX_RSS_MB = MAX_RSS_KB / 1024.0  # 56299.664 MB (~56300 MB)

CAPACITIES = {
    "12.5pct": {
        "pct": 12.5,
        "mb": MAX_RSS_MB * 0.125,  # 7037.458 MB
    },
    "25pct": {
        "pct": 25.0,
        "mb": MAX_RSS_MB * 0.25,   # 14074.916 MB
    },
    "50pct": {
        "pct": 50.0,
        "mb": MAX_RSS_MB * 0.50,   # 28149.832 MB
    }
}

def load_sites():
    if not os.path.exists(SITES_PATH):
        print(f"[Guidance Generator] ERROR: Allocation sites file not found at '{SITES_PATH}'. "
              "Run the profiler first to generate it.", file=sys.stderr)
        sys.exit(1)
    with open(SITES_PATH, "r") as f:
        return json.load(f)

def generate_static_guidances(sites):
    os.makedirs(GUIDANCE_DIR, exist_ok=True)
    
    # 1. DDR-only (all tier 0)
    ddr_guidance = [{"site_id": s["site_id"], "tier": 0} for s in sites]
    with open(os.path.join(GUIDANCE_DIR, "ddr_only.json"), "w") as f:
        json.dump(ddr_guidance, f, indent=2)
    print(f"[Guidance Generator] Saved DDR-only guidance -> '{os.path.join(GUIDANCE_DIR, 'ddr_only.json')}'")

    # 2. HBM-only (all tier 2)
    hbm_guidance = [{"site_id": s["site_id"], "tier": 2} for s in sites]
    with open(os.path.join(GUIDANCE_DIR, "hbm_only.json"), "w") as f:
        json.dump(hbm_guidance, f, indent=2)
    print(f"[Guidance Generator] Saved HBM-only guidance -> '{os.path.join(GUIDANCE_DIR, 'hbm_only.json')}'")

def generate_optimized_guidances():
    sys.path.insert(0, os.path.join(ROOT_DIR, "optimizer"))
    from membrain_opt import (
        load_profile_data,
        run_knapsack_optimization,
        run_hotset_optimization,
        run_thermos_optimization,
        save_guidance_json
    )

    if not os.path.exists(PROFILE_PATH):
        print(f"[Guidance Generator] ERROR: Profile data not found at '{PROFILE_PATH}'. "
              "Run the profiler first to generate it.", file=sys.stderr)
        sys.exit(1)
    sites = load_profile_data(PROFILE_PATH)

    summary = {}
    for cap_key, cap_info in CAPACITIES.items():
        cap_mb = cap_info["mb"]
        cap_bytes = int(cap_mb * 1024 * 1024)
        cap_dir = os.path.join(GUIDANCE_DIR, cap_key)
        os.makedirs(cap_dir, exist_ok=True)

        print(f"\n==================================================================")
        print(f" Generating Guidance for Capacity: {cap_key} ({cap_info['pct']}%, {cap_mb:.2f} MB)")
        print(f"==================================================================")

        summary[cap_key] = {"capacity_mb": cap_mb, "strategies": {}}

        # Knapsack
        knap_guidance, knap_usage = run_knapsack_optimization(sites, cap_bytes)
        knap_path = os.path.join(cap_dir, "knapsack.json")
        save_guidance_json(knap_guidance, knap_path)
        knap_hbm_count = sum(1 for g in knap_guidance if g["tier"] == 2)
        summary[cap_key]["strategies"]["knapsack"] = {
            "hbm_sites": knap_hbm_count,
            "hbm_usage_mb": knap_usage / (1024 * 1024),
            "file": knap_path
        }

        # Hotset
        hot_guidance, hot_usage = run_hotset_optimization(sites, cap_bytes)
        hot_path = os.path.join(cap_dir, "hotset.json")
        save_guidance_json(hot_guidance, hot_path)
        hot_hbm_count = sum(1 for g in hot_guidance if g["tier"] == 2)
        summary[cap_key]["strategies"]["hotset"] = {
            "hbm_sites": hot_hbm_count,
            "hbm_usage_mb": hot_usage / (1024 * 1024),
            "file": hot_path
        }

        # Thermos
        therm_guidance, therm_usage = run_thermos_optimization(sites, cap_bytes)
        therm_path = os.path.join(cap_dir, "thermos.json")
        save_guidance_json(therm_guidance, therm_path)
        therm_hbm_count = sum(1 for g in therm_guidance if g["tier"] == 2)
        summary[cap_key]["strategies"]["thermos"] = {
            "hbm_sites": therm_hbm_count,
            "hbm_usage_mb": therm_usage / (1024 * 1024),
            "file": therm_path
        }

    with open(os.path.join(GUIDANCE_DIR, "guidance_summary.json"), "w") as f:
        json.dump(summary, f, indent=2)

    print("\n[Guidance Generator] All guidance files generated successfully.")

def main():
    sites = load_sites()
    generate_static_guidances(sites)
    generate_optimized_guidances()

if __name__ == "__main__":
    main()
