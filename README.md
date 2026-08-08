# MemBrain: Automated Application Guidance for Hybrid Memory Systems

**Codebase Author:** Maksym Shcherba (<maksym.shcherba@lnu.edu.ua>)  
**Based on Original Research by:** M. B. Olson, T. Zhou, M. R. Jantz, K. A. Doshi, M. G. Lopez, and O. Hernandez (IEEE NAS 2018)

MemBrain is an automated data placement framework for single-socket hybrid memory architectures (Intel Xeon Max Sapphire Rapids with **DDR5 on NUMA Node 0** and **HBM2e Flat Mode on NUMA Node 2**).

> **Note:** This repository is an independent research implementation of the MemBrain framework tailored for single-socket Intel Xeon Max systems.



## 📚 Citation & Reference

If you use this codebase or reference the MemBrain concept, please cite the original paper:

> M. B. Olson, T. Zhou, M. R. Jantz, K. A. Doshi, M. G. Lopez and O. Hernandez, "MemBrain: Automated Application Guidance for Hybrid Memory Systems," *2018 IEEE International Conference on Networking, Architecture and Storage (NAS)*, Chongqing, China, 2018, pp. 1-10, doi: 10.1109/NAS.2018.8515694.

```bibtex
@INPROCEEDINGS{8515694,
  author={Olson, M. Ben and Zhou, Tong and Jantz, Michael R. and Doshi, Kshitij A. and Lopez, M. Graham and Hernandez, Oscar},
  booktitle={2018 IEEE International Conference on Networking, Architecture and Storage (NAS)}, 
  title={MemBrain: Automated Application Guidance for Hybrid Memory Systems}, 
  year={2018},
  volume={},
  number={},
  pages={1-10},
  keywords={Bandwidth;Resource management;Memory management;Random access memory;Tools;Hardware;Runtime},
  doi={10.1109/NAS.2018.8515694}}
```

---


## 📁 Repository Structure

- `llvm-pass/` — LLVM Compiler Pass plugin (`MemBrainPass.so`) for allocation rewriting, `site_id` tagging, and static function cloning ($n=4$).
- `runtime/` — Dynamic interposition runtime library (`libmembrain_rt.so`) with `mbind(MPOL_PREFERRED)` NUMA memory binding.
- `profiler/` — Memory profilers:
  - `pebs_profiler.py`: PEBS-based access sampling & peak RSS tracking.
  - `mbi_profiler.py`: Memory Bandwidth Isolation (MBI) per-site bandwidth profiler.
- `optimizer/` — Placement guidance bin-packing optimizer (`membrain_opt.py`) supporting **Thermos**, **Hotset**, and **0/1 Knapsack (DP)** algorithms.
- `scripts/` — Automated build and benchmark evaluation pipelines:
  - `build_lulesh_membrain.sh`: Isolated compilation of LULESH 2.0 with MemBrain LLVM Pass.
  - `run_lulesh_eval.sh`: End-to-end evaluation pipeline comparing baseline Intel LULESH vs MemBrain-guided execution.

---

## 🛠️ Requirements & Setup

- **Hardware Platform:** Intel Xeon Max (Sapphire Rapids HBM2e + DDR5).
- **NUMA Configuration:** Single-socket execution (NUMA 0: DDR5, NUMA 2: HBM2e Flat Mode).
- **Software Dependencies:**
  - `LLVM 18` / `clang++` or `icpx`
  - `libnuma-dev`
  - `Python 3`
  - `numactl`

---

## 🚀 Usage Guide

### 1. Build MemBrain Infrastructure
Build the LLVM Pass plugin and Runtime library:
```bash
mkdir -p build && cd build
cmake ..
make -j $(nproc)
cd ..
```

### 2. Build Unified LULESH Binary
To build the unified LULESH 2.0 binary with Intel compiler and MemBrain integration in `LULESH/build/`:
```bash
./scripts/build_lulesh.sh
```
This generates `/users/maksym/LULESH/build/lulesh2.0`.

### 3. Profiling Allocation Sites
Generate access frequency and bandwidth profiles:
```bash
# PEBS Profiler (Access counts + Peak RSS)
python3 profiler/pebs_profiler.py \
    --sites LULESH/build/allocation_sites.json \
    --runtime build/runtime/libmembrain_rt.so \
    --output profile_data.json \
    LULESH/build/lulesh2.0 -s 10 -i 5

# Memory Bandwidth Isolation (MBI) Profiler
python3 profiler/mbi_profiler.py \
    --sites LULESH/build/allocation_sites.json \
    --runtime build/runtime/libmembrain_rt.so \
    --output mbi_profile_data.json \
    LULESH/build/lulesh2.0 -s 10 -i 5
```

### 4. Running Optimization Algorithms
Generate placement guidance (`site_tier_guidance.json`) using one of the three supported bin-packing strategies:
```bash
# Thermos Strategy (Greedy with Displacement Thresholding)
python3 optimizer/membrain_opt.py --profile profile_data.json --strategy thermos --hbm-capacity-mb 65536.0

# Hotset Strategy (Soft Capacity Limit)
python3 optimizer/membrain_opt.py --profile profile_data.json --strategy hotset --hbm-capacity-mb 65536.0

# 0/1 Knapsack Strategy (Dynamic Programming)
python3 optimizer/membrain_opt.py --profile profile_data.json --strategy knapsack --hbm-capacity-mb 65536.0
```

### 5. Running MemBrain-Guided Binary
Execute the instrumented binary with generated placement guidance:
```bash
LD_LIBRARY_PATH="$(pwd)/build/runtime:${LD_LIBRARY_PATH}" \
MEMBRAIN_VERBOSE=1 \
LULESH/build/lulesh2.0 -s 420 -i 5 -r 11 -b 0 -c 64 -p
```

### 6. Automated End-to-End Evaluation Pipeline
Run a complete baseline vs MemBrain evaluation comparison (defaults to `-s 420 -i 5 -r 11 -b 0 -c 64 -p`):
```bash
./scripts/run_lulesh_eval.sh
```
Or pass custom arguments:
```bash
./scripts/run_lulesh_eval.sh -s 420 -i 5 -r 11 -b 0 -c 64 -p
```

This script automatically executes baseline Intel LULESH, profiles allocation sites, calculates placement guidance, runs MemBrain-guided execution, and outputs a formatted performance speedup summary table.

