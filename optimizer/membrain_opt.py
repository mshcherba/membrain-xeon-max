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

    Formulates data placement as a 0/1 knapsack problem solved via SciPy MILP (HiGHS):
    - Item weight: static 4 KB page count (rss_bytes // 4096)
    - Item value: total access count
    - Capacity limit W: hbm_capacity_bytes // 4096
    """
    scale_factor = 4096 
    W = max(1, int(hbm_capacity_bytes // scale_factor))
    n = len(sites)
    if n == 0:
        return [], 0

    weights = [max(1, int(s.get("rss_bytes", 0) // scale_factor)) for s in sites]
    values = [int(s.get("access_count", 0)) for s in sites]

    # Fast path: If all items fit within capacity, select all directly
    if sum(weights) <= W:
        hbm_sites = {s["site_id"] for s in sites}
        current_hbm_usage = sum(s.get("rss_bytes", 0) for s in sites)
        guidance = [{"site_id": s["site_id"], "tier": 2} for s in sites]
        return guidance, current_hbm_usage

    from scipy.optimize import milp, LinearConstraint, Bounds
    import numpy as np

    # 0/1 Knapsack formulation via MILP:
    # Maximize sum(values[i] * x[i]) <==> Minimize sum(-values[i] * x[i])
    # Subject to: sum(weights[i] * x[i]) <= W
    # Bounds: 0 <= x[i] <= 1
    # Integrality: 1 (binary integer)
    c = -np.array(values, dtype=float)
    A = np.array(weights, dtype=float)
    constraints = LinearConstraint(A, 0, W)
    integrality = np.ones(n)
    bounds = Bounds(0, 1)

    res = milp(c=c, constraints=constraints, bounds=bounds, integrality=integrality)

    if not res.success:
        raise RuntimeError(
            f"[MemBrain Optimizer] Knapsack MILP solver failed (status {res.status}): {res.message}"
        )

    hbm_sites = {sites[i]["site_id"] for i, val in enumerate(res.x) if val > 0.5}

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

    Paper Specification:
      "Thermos is similar to hotset with one exception: it only assigns a new site
       to the upper tier if the bandwidth it contributes is greater than the aggregate
       bandwidth of the hottest data it could potentially displace. In this way,
       thermos avoids crowding out performance-critical data, while still allowing
       large-capacity, high-bandwidth sites to place a portion of their data in the
       upper-level memory."

    Algorithm:
    1. Sort sites by hotness = (access_count / rss_bytes) in descending order.
    2. Add sites to HBM as long as (current_hbm_usage + site_rss <= hbm_capacity_bytes).
    3. When a candidate site causes HBM usage to exceed capacity:
       - Overflow amount: overflow_bytes = (current_hbm_usage + site_rss) - hbm_capacity_bytes.
       - Displaceable capacity in HBM: displaceable_bytes = min(overflow_bytes, current_hbm_usage).
       - Displaced bandwidth: Aggregate bandwidth of the *hottest* data (starting from the
         top admitted sites in descending order of hotness) up to displaceable_bytes.
       - Contributed bandwidth: Bandwidth provided by the portion of the candidate site that
         actually resides in the upper tier:
           admitted_rss = min(site_rss, hbm_capacity_bytes - current_hbm_usage + displaceable_bytes)
           contributed_bandwidth = admitted_rss * site_hotness
       - Displacement condition:
         If contributed_bandwidth > displaced_bandwidth:
           Admit site to HBM, update current_hbm_usage, and terminate (upper tier saturated).
         Else:
           Do NOT admit this site; continue checking subsequent smaller candidates that may fit.
    """
    sorted_sites = sorted(sites, key=lambda x: x.get("hotness", 0), reverse=True)
    current_hbm_usage = 0
    admitted_sites = []
    hbm_sites = set()

    for site in sorted_sites:
        site_id = site["site_id"]
        site_rss = site.get("rss_bytes", 0)
        site_access = site.get("access_count", 0)
        site_hotness = site.get("hotness", 0.0)
        if site_rss > 0 and site_hotness == 0.0 and site_access > 0:
            site_hotness = site_access / site_rss

        if current_hbm_usage >= hbm_capacity_bytes:
            break

        if current_hbm_usage + site_rss <= hbm_capacity_bytes:
            hbm_sites.add(site_id)
            admitted_sites.append({
                "site_id": site_id,
                "rss_bytes": site_rss,
                "access_count": site_access,
                "hotness": site_hotness
            })
            current_hbm_usage += site_rss
        else:
            overflow_bytes = (current_hbm_usage + site_rss) - hbm_capacity_bytes
            displaceable_bytes = min(overflow_bytes, current_hbm_usage)

            # Aggregate bandwidth of the HOTTEST data potentially displaced
            displaced_bandwidth = 0.0
            bytes_to_displace = displaceable_bytes
            for adm in admitted_sites:
                if bytes_to_displace <= 0:
                    break
                take_bytes = min(adm["rss_bytes"], bytes_to_displace)
                displaced_bandwidth += take_bytes * adm["hotness"]
                bytes_to_displace -= take_bytes

            # Contributed bandwidth from the portion residing in HBM
            effective_available = hbm_capacity_bytes - current_hbm_usage + displaceable_bytes
            admitted_rss = min(site_rss, effective_available)
            contributed_bandwidth = admitted_rss * site_hotness

            if contributed_bandwidth > displaced_bandwidth:
                hbm_sites.add(site_id)
                current_hbm_usage += site_rss
                break

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
