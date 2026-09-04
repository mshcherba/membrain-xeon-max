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
  common_config.json      — global OpenMP, affinity, and clone depth configuration
  common_env.sh           — environment setup script for OpenMP and affinity settings
  lulesh/                 — LULESH 2.0 evaluation pipeline
    build.sh              — 2-stage build (LLVM Pass + Intel icpx linking against runtime)
    config.json           — benchmark configuration (binary path, arguments, FOM regex)
    profile.sh            — in-process PEBS profiling wrapper script
    run_suite.sh          — shortcut to run the 14-configuration tiering suite
    guidance/             — generated placement guidance JSON files
    experiments/          — experiment logs and summary reports (Markdown / JSON)
  bfs/                    — Graph500 Breadth-First Search evaluation pipeline
    build.sh              — 2-stage BFS build with MemBrain LLVM pass
    config.json           — BFS configuration and TEPS FOM patterns
    profile.sh            — in-process PEBS profiling wrapper script
    run_suite.sh          — tiering suite runner shortcut
    guidance/             — generated placement guidance JSON files
    experiments/          — experiment logs and summary reports
  qmcpack/                — QMCPACK 4.3.9 Quantum Monte Carlo pipeline
    build.sh              — QMCPACK build script with MemBrain LLVM pass
    config.json           — QMCPACK configuration and FOM metric patterns
    profile.sh            — in-process PEBS profiling wrapper script
    run_suite.sh          — tiering suite runner shortcut
    NiO-fcc-S64.xml       — NiO S64 workload input definition

scripts/                  — generic execution and system orchestration scripts
  run_benchmark_suite.py  — multi-benchmark tiering suite runner (orchestrates RSS measurement,
                            guidance generation, 14-run matrix, and FOM statistics with 95% CI)
  set_hbm_capacity.sh     — constrain HBM via 2 MB hugepage pre-allocation
  prepare_system.sh       — system configuration (NUMA, hugepages, perf)
  install_dependencies.sh — Intel oneAPI + LLVM dependency installer
  merge_allocation_sites.py — merge per-TU allocation_sites JSON files

llvm-pass/                — LLVM MemBrainPass plugin (site tagging + call-path function cloning)
runtime/                  — libmembrain_rt.so (Intel UMF scalable pools + mbind NUMA interposition)
profiler/                 — pebs_profiler.py
optimizer/                — membrain_opt.py (Knapsack MILP, Hotset, Thermos strategies)
tests/                    — unit tests for statistics, pagemap, and runtime
```

---

## 🛠️ Requirements & Setup

- **Hardware Platform:** Intel Xeon Max (Sapphire Rapids HBM2e + DDR5).
- **NUMA Configuration:** Single-socket execution (NUMA 0: DDR5, NUMA 2: HBM2e Flat Mode).
- **Software Dependencies:**
  - `LLVM 18` / `clang++` or Intel `icpx` (oneAPI)
  - `libnuma-dev`
  - `Python 3` with `numpy` and `scipy`
  - `numactl`
  - Dependencies can be installed via [`scripts/install_dependencies.sh`](scripts/install_dependencies.sh):
    ```bash
    sudo ./scripts/install_dependencies.sh
    ```

### System Preparation
Before profiling or running benchmarks, the host system environment must be configured by running [`scripts/prepare_system.sh`](scripts/prepare_system.sh) with root privileges:
```bash
sudo ./scripts/prepare_system.sh
```
This script configures kernel, CPU, and memory settings for reproducible and isolated benchmark execution:
- Disables swapping (`swapoff -a` & `vm.swappiness=0`)
- Disables Transparent Huge Pages (THP)
- Disables automatic NUMA balancing (`kernel.numa_balancing=0`)
- Disables SMT / Hyper-Threading
- Sets CPU scaling governor to `performance`
- Sets `kernel.perf_event_paranoid=-1` for hardware PEBS sampling

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

### 2. Build Unified Benchmark Binary
For example, build LULESH 2.0 with the 2-stage pipeline:
```bash
./benchmarks/lulesh/build.sh
```
Runs Stage A (LLVM bitcode generation + `llvm-link` whole-program linking + post-optimization `MemBrainPass` transformation), generating whole-program `allocation_sites.json`, and outputs the binary linked with `libmembrain_rt.so`.

> **Call Path Function Cloning Depth:** In accordance with Section IV-A & V-C of the paper, MemBrain provides configurable call path function cloning depth $n$. The default depth is **4** (evaluated in the paper). This can be customized by exporting `MEMBRAIN_CLONE_DEPTH=<n>`, or set to `0` to disable cloning.

### 3. Profiling Allocation Sites
Generate access frequency and memory profile data using the in-process PEBS profiler:
```bash
# PEBS Profiler (In-process LLC Miss Access Counts + Physical Peak RSS via pagemap)
./benchmarks/lulesh/profile.sh
```

### 4. Running Optimization Algorithms
Generate placement guidance using one of the three supported bin-packing strategies:
```bash
# 0/1 Knapsack Strategy (Mixed-Integer Linear Programming via SciPy HiGHS)
python3 optimizer/membrain_opt.py --profile benchmarks/lulesh/profile_data.json --strategy knapsack --hbm-capacity-mb 16384.0

# Hotset Strategy (Soft Capacity Limit Boundary Inclusion)
python3 optimizer/membrain_opt.py --profile benchmarks/lulesh/profile_data.json --strategy hotset --hbm-capacity-mb 16384.0

# Thermos Strategy (Displacement Thresholding against Hottest Potentially Displaced Data)
python3 optimizer/membrain_opt.py --profile benchmarks/lulesh/profile_data.json --strategy thermos --hbm-capacity-mb 16384.0
```

### 5. Running MemBrain-Guided Binary Directly
Execute an instrumented binary with generated placement guidance:
```bash
MEMBRAIN_GUIDANCE_PATH="benchmarks/lulesh/guidance/25pct/thermos.json" \
LD_PRELOAD="$(pwd)/build/runtime/libmembrain_rt.so" \
MEMBRAIN_VERBOSE=1 \
numactl --cpunodebind=0 --preferred=2 benchmarks/lulesh/build/lulesh2.0 -s 400 -i 5 -r 11 -b 0 -c 64 -p
```

### 6. Running Constrained HBM Tiering Suite
The repository includes a unified multi-benchmark runner ([`scripts/run_benchmark_suite.py`](scripts/run_benchmark_suite.py)) that automatically:
1. Measures workload Peak RSS under unconstrained HBM.
2. Generates guidance for all strategy and capacity combinations.
3. Clamps HBM free memory via hugepages reservation ([`scripts/set_hbm_capacity.sh`](scripts/set_hbm_capacity.sh)).
4. Executes the full **14-configuration evaluation matrix**:
   - 2 Unconstrained Baselines: DDR-only (`--membind=0`) and HBM-only (`--membind=2`, 100% reference baseline).
   - 12 Constrained Configurations: 3 capacities (**12.5%, 25.0%, 50.0%** of peak RSS) $\times$ 4 strategies (`first-touch`, `knapsack`, `hotset`, `thermos`).
5. Performs $N$ repeated runs (default: 5) and computes comprehensive FOM statistics (Mean $\pm$ 95% Confidence Interval, StdDev, relative speedup vs HBM).

To run the suite for LULESH:
```bash
./benchmarks/lulesh/run_suite.sh
# Or invoke runner directly with custom parameters:
python3 scripts/run_benchmark_suite.py --benchmark lulesh --repeats 5 --capacities 12.5 25.0 50.0
```

To run for other supported benchmarks:
```bash
# Graph500 Breadth-First Search
./benchmarks/bfs/run_suite.sh

# QMCPACK Quantum Monte Carlo
./benchmarks/qmcpack/run_suite.sh
```

### 7. Results & Reports
Results are exported directly into the benchmark's `experiments/results/` directory:
- `benchmark_summary.md` — Formatted Markdown tables with Mean FOM, 95% Margin of Error, and Relative Speedup vs HBM-only.
- `benchmark_results.json` — Machine-readable structured results containing raw and aggregated metrics.
- `experiments/logs/` — Individual stdout/stderr logs for every repetition.

### 8. Running Unit Tests
Validate the statistical runner, pagemap utility, and runtime components:
```bash
python3 -m unittest discover -s tests -v
```
