#!/usr/bin/env python3
"""
MemBrain Bin-Packing Optimizer (Thermos / Hotset / Knapsack)
Generates site_tier_guidance.json for MemBrain Runtime.
"""

import os
import sys
import json
import argparse

def load_profile_data(profile_path):
    if not os.path.exists(profile_path):
        print(f"[MemBrain Optimizer] Error: Profile file '{profile_path}' not found!", file=sys.stderr)
        sys.exit(1)
    with open(profile_path, 'r') as f:
        return json.load(f)

def run_knapsack_optimization(sites, hbm_capacity_bytes):
    """
    0/1 Knapsack Optimization Strategy (MemBrain Paper Section III-A).

    Formulates data placement as a 0/1 knapsack problem:
    - Item weight: static 4 KB page count (rss_bytes // 4096)
    - Item value: total access count
    - Capacity limit W: hbm_capacity_bytes // 4096
    """
    scale_factor = 4096 
    W = max(1, int(hbm_capacity_bytes // scale_factor))
    n = len(sites)

    weights = [max(1, int(s.get("rss_bytes", 0) // scale_factor)) for s in sites]
    values = [int(s.get("access_count", 0)) for s in sites]

    dp = [[0] * (W + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        for w in range(1, W + 1):
            if weights[i - 1] <= w:
                dp[i][w] = max(dp[i - 1][w], dp[i - 1][w - weights[i - 1]] + values[i - 1])
            else:
                dp[i][w] = dp[i - 1][w]

    # Backtrack DP table to identify selected items
    hbm_sites = set()
    w = W
    for i in range(n, 0, -1):
        if dp[i][w] != dp[i - 1][w]:
            hbm_sites.add(sites[i - 1]["site_id"])
            w -= weights[i - 1]

    current_hbm_usage = sum(s.get("rss_bytes", 0) for s in sites if s["site_id"] in hbm_sites)
    guidance = [{"site_id": s["site_id"], "tier": 2 if s["site_id"] in hbm_sites else 0} for s in sites]
    return guidance, current_hbm_usage

def run_hotset_optimization(sites, hbm_capacity_bytes):
    """
    Hotset Optimization Strategy (MemBrain Paper Section III-A).

    Algorithm:
    - Sort allocation sites by hotness = (access_count / rss_bytes) in descending order.
    - Soft Capacity Behavior: Adds sites sequentially into HBM until aggregate capacity
      meets or exceeds hbm_capacity_bytes. The site that causes total usage to cross
      the capacity threshold is included (soft capacity limit) before iteration stops.
    """
    sorted_sites = sorted(sites, key=lambda x: x.get("hotness", 0), reverse=True)
    current_hbm_usage = 0
    hbm_sites = set()

    for site in sorted_sites:
        site_id = site["site_id"]
        site_rss = site.get("rss_bytes", 0)
        
        # Soft capacity behavior: site is added first before checking capacity limit
        hbm_sites.add(site_id)
        current_hbm_usage += site_rss
        if current_hbm_usage >= hbm_capacity_bytes:
            break

    guidance = [{"site_id": s["site_id"], "tier": 2 if s["site_id"] in hbm_sites else 0} for s in sites]
    return guidance, current_hbm_usage

def run_thermos_optimization(sites, hbm_capacity_bytes):
    """
    Thermos Data Placement Optimization Strategy (MemBrain Paper Section III-A).

    Algorithm:
    1. Sort sites by hotness = (access_count / rss_bytes) in descending order.
    2. Add sites to HBM as long as (current_hbm_usage + site_rss <= hbm_capacity_bytes).
    3. When a site causes HBM usage to exceed capacity (current_hbm_usage + site_rss > hbm_capacity_bytes):
       - Compute overflow_bytes = (current_hbm_usage + site_rss) - hbm_capacity_bytes
       - Compute current average HBM bandwidth density:
         hbm_density = total_hbm_access / current_hbm_usage
       - Compute displaced bandwidth cost:
         displaced_bandwidth = overflow_bytes * hbm_density
       - Assign site to HBM if and only if site_access > displaced_bandwidth.
    """
    sorted_sites = sorted(sites, key=lambda x: x.get("hotness", 0), reverse=True)
    current_hbm_usage = 0
    total_hbm_access = 0
    hbm_sites = set()

    for site in sorted_sites:
        site_id = site["site_id"]
        site_rss = site.get("rss_bytes", 0)
        site_access = site.get("access_count", 0)

        if current_hbm_usage >= hbm_capacity_bytes:
            # HBM capacity is full or exceeded. Because sites are sorted by hotness descending,
            # any remaining site's hotness is <= hbm_density, so it can never pass the threshold.
            break

        if current_hbm_usage + site_rss <= hbm_capacity_bytes:
            hbm_sites.add(site_id)
            current_hbm_usage += site_rss
            total_hbm_access += site_access
        else:
            # Thermos displacement threshold check:
            # Only assign the site if its contributed bandwidth (site_access) is greater
            # than the aggregate bandwidth of the data it would displace from HBM.
            overflow_bytes = (current_hbm_usage + site_rss) - hbm_capacity_bytes
            hbm_density = (total_hbm_access / current_hbm_usage) if current_hbm_usage > 0 else 0
            displaced_bandwidth = overflow_bytes * hbm_density

            if site_access > displaced_bandwidth:
                hbm_sites.add(site_id)
                current_hbm_usage += site_rss
                total_hbm_access += site_access

    guidance = [{"site_id": s["site_id"], "tier": 2 if s["site_id"] in hbm_sites else 0} for s in sites]
    return guidance, current_hbm_usage

def save_guidance_json(guidance, output_path):
    with open(output_path, "w") as f:
        json.dump(guidance, f, indent=2)
    print(f"[MemBrain Optimizer] Guidance saved to '{output_path}' ({len(guidance)} sites processed).")

def main():
    parser = argparse.ArgumentParser(description="MemBrain Bin-Packing Optimizer")
    parser.add_argument("--profile", default="profile_data.json", help="Path to profile_data.json")
    parser.add_argument("--output", default="site_tier_guidance.json", help="Path to output site_tier_guidance.json")
    parser.add_argument("--hbm-capacity-mb", type=float, default=65536.0, help="HBM2e capacity limit in MB (default: 64GB)")
    parser.add_argument("--strategy", choices=["thermos", "hotset", "knapsack"], default="thermos", help="Optimization strategy")

    args = parser.parse_args()
    sites = load_profile_data(args.profile)
    hbm_capacity_bytes = int(args.hbm_capacity_mb * 1024 * 1024)

    print(f"[MemBrain Optimizer] Running {args.strategy.upper()} strategy for HBM capacity: {args.hbm_capacity_mb:.1f} MB")

    if args.strategy == "hotset":
        guidance, usage_bytes = run_hotset_optimization(sites, hbm_capacity_bytes)
    elif args.strategy == "knapsack":
        guidance, usage_bytes = run_knapsack_optimization(sites, hbm_capacity_bytes)
    else:
        guidance, usage_bytes = run_thermos_optimization(sites, hbm_capacity_bytes)

    hbm_count = sum(1 for g in guidance if g["tier"] == 2)
    ddr_count = sum(1 for g in guidance if g["tier"] == 0)
    print(f"[MemBrain Optimizer] Result: {hbm_count} sites -> HBM2e ({usage_bytes / (1024*1024):.2f} MB), {ddr_count} sites -> DDR5")

    save_guidance_json(guidance, args.output)

if __name__ == "__main__":
    main()
