#!/usr/bin/env python3
"""
MemBrain Allocation Sites Merger
Merges per-module allocation_sites_*.json files into a single unified allocation_sites.json file.
"""

import os
import sys
import glob
import json
import argparse

def merge_allocation_sites(input_dir, output_file):
    pattern = os.path.join(input_dir, "allocation_sites_*.json")
    files = sorted(glob.glob(pattern))

    merged_sites = []
    seen_site_ids = set()

    for fpath in files:
        if os.path.abspath(fpath) == os.path.abspath(output_file):
            continue
        try:
            with open(fpath, 'r') as f:
                data = json.load(f)
                if isinstance(data, list):
                    for item in data:
                        sid = item.get("site_id")
                        if sid in seen_site_ids:
                            print(f"[MemBrain Merger] WARNING: Collision detected for site_id {sid} in '{os.path.basename(fpath)}'", file=sys.stderr)
                        else:
                            merged_sites.append(item)
                            seen_site_ids.add(sid)
        except Exception as e:
            print(f"[MemBrain Merger] Warning: failed to read {fpath}: {e}", file=sys.stderr)

    with open(output_file, 'w') as f:
        json.dump(merged_sites, f, indent=2)

    print(f"[MemBrain Merger] Merged {len(merged_sites)} allocation sites from {len(files)} module files -> '{output_file}'")

def main():
    parser = argparse.ArgumentParser(description="Merge per-module allocation_sites_*.json files")
    parser.add_argument("--dir", default=".", help="Directory containing allocation_sites_*.json files")
    parser.add_argument("--output", default="allocation_sites.json", help="Output merged JSON file path")
    args = parser.parse_args()

    merge_allocation_sites(args.dir, args.output)

if __name__ == "__main__":
    main()
