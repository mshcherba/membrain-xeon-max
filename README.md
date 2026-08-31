# MemBrain: Automated Application Guidance for Hybrid Memory Systems

**Codebase Author:** Maksym Shcherba (<maksym.shcherba@lnu.edu.ua>)  
**Based on Original Research by:** M. B. Olson, T. Zhou, M. R. Jantz, K. A. Doshi, M. G. Lopez, and O. Hernandez (IEEE NAS 2018)

MemBrain is an automated data placement framework for single-socket hybrid memory architectures (Intel Xeon Max Sapphire Rapids with **DDR5 on NUMA Node 0** and **HBM2e Flat Mode on NUMA Node 2**).

> **Note:** This repository is an independent research implementation of the MemBrain framework tailored for single-socket Intel Xeon Max systems.

---

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

```
benchmarks/
  lulesh/               — LULESH 2.0 evaluation pipeline
    build.sh            — 2-stage MemBrain build (LLVM Pass + Intel icpx)
    run_single.sh       — single baseline run (ddr/hbm mode)
    run_repeats.sh      — repeated runs for statistical analysis
    eval.sh             — end-to-end baseline vs guided comparison
    profile.sh          — PEBS profiling wrapper
    measure_time.sh     — DDR-only /usr/bin/time -v RSS baseline
    generate_guidance.py — generate all guidance configs (3 capacities × 3 strategies)
    run_tiering_suite.py — orchestrate all 14 tiering experiments
    run_tiering_suite.sh — shell wrapper for the tiering suite
    analyze_fom.py      — FOM statistical analysis (mean, CI, std)
    test_direct.sh      — smoke test: bare LULESH run
    test_guided.sh      — smoke test: MemBrain runtime preloaded
    guidance/           — generated placement guidance JSON files
    experiments/        — experiment logs and result summaries
  <next-benchmark>/     — follow the same layout for future benchmarks

scripts/                — benchmark-agnostic system scripts
  set_hbm_capacity.sh   — constrain HBM via 2 MB hugepage pre-allocation
  prepare_system.sh     — system configuration (NUMA, hugepages, perf)
  install_dependencies.sh — oneAPI + LLVM dependency installer
  merge_allocation_sites.py — merge per-TU allocation_sites JSON files

llvm-pass/              — LLVM MemBrainPass plugin (site tagging + function cloning)
runtime/                — libmembrain_rt.so (UMF pools + mbind NUMA interposition)
profiler/               — pebs_profiler.py, mbi_profiler.py, profiler_core.py
optimizer/              — membrain_opt.py (Knapsack, Hotset, Thermos strategies)
```

---

## 🛠️ Requirements & Setup

- **Hardware Platform:** Intel Xeon Max (Sapphire Rapids HBM2e + DDR5).
- **NUMA Configuration:** Single-socket execution (NUMA 0: DDR5, NUMA 2: HBM2e Flat Mode).
- **Software Dependencies:**
  - `LLVM 18` / `clang++` or Intel `icpx`
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
```bash
./benchmarks/lulesh/build.sh
```
Runs Stage A (LLVM bitcode generation + `llvm-link` whole-program linking + post-optimization `MemBrainPass` transformation), generating whole-program `LULESH/build/allocation_sites.json`, and outputs the binary at `LULESH/build/lulesh2.0`.

> **Call Path Function Cloning Depth:** In accordance with Section IV-A & V-C of the paper, MemBrain provides configurable call path function cloning depth $n$. The default depth is **4** (evaluated in the paper). This can be customized by exporting `MEMBRAIN_CLONE_DEPTH=<n>`, or set to `0` to disable cloning.

### 3. Profiling Allocation Sites
Generate access frequency and bandwidth profiles:
```bash
# PEBS Profiler (Access counts + Peak RSS)
./benchmarks/lulesh/profile.sh

# Memory Bandwidth Isolation (MBI) Profiler
python3 profiler/mbi_profiler.py \
    --sites LULESH/build/allocation_sites.json \
    --runtime build/runtime/libmembrain_rt.so \
    --output benchmarks/lulesh/mbi_profile_data.json \
    LULESH/build/lulesh2.0 -s 400 -i 5 -r 11 -b 0 -c 64 -p
```

### 4. Running Optimization Algorithms
Generate placement guidance using one of the three supported bin-packing strategies:
```bash
# 0/1 Knapsack Strategy (Dynamic Programming)
python3 optimizer/membrain_opt.py --profile benchmarks/lulesh/profile_data.json --strategy knapsack --hbm-capacity-mb 65536.0

# Hotset Strategy (Soft Capacity Limit)
python3 optimizer/membrain_opt.py --profile benchmarks/lulesh/profile_data.json --strategy hotset --hbm-capacity-mb 65536.0

# Thermos Strategy (Greedy Selection with Displacement Thresholding)
python3 optimizer/membrain_opt.py --profile benchmarks/lulesh/profile_data.json --strategy thermos --hbm-capacity-mb 65536.0
```

### 5. Running MemBrain-Guided Binary
Execute the instrumented binary with generated placement guidance:
```bash
MEMBRAIN_GUIDANCE_PATH="benchmarks/lulesh/guidance/ddr_only.json" \
LD_PRELOAD="$(pwd)/build/runtime/libmembrain_rt.so" \
MEMBRAIN_VERBOSE=1 \
numactl --cpunodebind=0 LULESH/build/lulesh2.0 -s 400 -i 5 -r 11 -b 0 -c 64 -p
```

### 6. Full End-to-End Evaluation
Run a complete baseline vs MemBrain evaluation comparison:
```bash
./benchmarks/lulesh/eval.sh
```

### 7. Constrained HBM Tiering Experiment Suite
Generate all guidance configurations and run all 14 experiments (3 capacities × 3 strategies + 2 unconstrained baselines):
```bash
./benchmarks/lulesh/run_tiering_suite.sh
```
Results are saved to `benchmarks/lulesh/experiments/results/`.

### 8. Statistical Benchmark Analysis
Run repeated LULESH executions and compute FOM statistics with 95% CI:
```bash
./benchmarks/lulesh/run_repeats.sh 5 hbm | python3 ./benchmarks/lulesh/analyze_fom.py
```

