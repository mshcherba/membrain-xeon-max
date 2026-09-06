# MemBrain Benchmark Results: LULESH 2.0 (Livermore Unstructured Lagrangian Explicit Shock Hydrodynamics)

- **Platform:** Intel Xeon Max (Flat Mode HBM2e + DDR5)
- **Workload Arguments:** `-s 400 -i 5 -r 11 -b 0 -c 64 -p`
- **Repetitions:** 5 runs per configuration (Mean $\pm$ 95% Confidence Interval)
- **Peak Measured RSS:** 57,652,392 KB (56301.2 MB)
- **Primary Metric:** FOM (z/s)
- **Reference Baseline:** **HBM-only (Unconstrained)** ($100.0\%$, $1.00\times$)
- **Date:** 2026-09-04 08:20:26

## 1. Overall FOM & Performance Comparison

| Configuration | HBM Cap (% / MB) | Mean FOM (z/s) | StdDev | 95% Margin of Error | Relative FOM vs HBM |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **DDR-only (Unconstrained)** | Unconstrained | **2066.95** | ±2.58 | ±3.20 (±0.2%) | **59.9%** (0.60x) |
| **HBM-only (Unconstrained 64GB)** | Unconstrained | **3450.05** | ±12.24 | ±15.19 (±0.4%) | **100.0%** (1.00x) |
| **12.5% HBM - First-Touch** | 12.5% (7038 MB) | **2212.30** | ±0.71 | ±0.88 (±0.0%) | **64.1%** (0.64x) |
| **12.5% HBM - Knapsack** | 12.5% (7038 MB) | **3331.78** | ±16.14 | ±20.04 (±0.6%) | **96.6%** (0.97x) |
| **12.5% HBM - Hotset** | 12.5% (7038 MB) | **2325.04** | ±8.53 | ±10.59 (±0.5%) | **67.4%** (0.67x) |
| **12.5% HBM - Thermos** | 12.5% (7038 MB) | **3393.63** | ±23.44 | ±29.11 (±0.9%) | **98.4%** (0.98x) |
| **25% HBM - First-Touch** | 25.0% (14075 MB) | **2270.08** | ±56.84 | ±70.57 (±3.1%) | **65.8%** (0.66x) |
| **25% HBM - Knapsack** | 25.0% (14075 MB) | **3415.04** | ±28.56 | ±35.46 (±1.0%) | **99.0%** (0.99x) |
| **25% HBM - Hotset** | 25.0% (14075 MB) | **3392.73** | ±9.39 | ±11.65 (±0.3%) | **98.3%** (0.98x) |
| **25% HBM - Thermos** | 25.0% (14075 MB) | **3452.79** | ±3.40 | ±4.22 (±0.1%) | **100.1%** (1.00x) |
| **50% HBM - First-Touch** | 50.0% (28151 MB) | **3390.22** | ±11.69 | ±14.51 (±0.4%) | **98.3%** (0.98x) |
| **50% HBM - Knapsack** | 50.0% (28151 MB) | **3370.23** | ±7.14 | ±8.87 (±0.3%) | **97.7%** (0.98x) |
| **50% HBM - Hotset** | 50.0% (28151 MB) | **3423.26** | ±12.61 | ±15.66 (±0.5%) | **99.2%** (0.99x) |
| **50% HBM - Thermos** | 50.0% (28151 MB) | **3386.53** | ±7.80 | ±9.68 (±0.3%) | **98.2%** (0.98x) |

## 2. Capacity-by-Capacity Breakdown

### Capacity Tier 12.5% (7037.6 MB HBM Budget)

| Strategy | Mean FOM (z/s) | 95% Confidence Interval | Relative FOM vs HBM |
| :--- | :--- | :--- | :--- |
| **FIRST-TOUCH** | **2212.30** | [2211.42, 2213.18] | **64.1%** (0.64x) |
| **KNAPSACK** | **3331.78** | [3311.74, 3351.81] | **96.6%** (0.97x) |
| **HOTSET** | **2325.04** | [2314.44, 2335.63] | **67.4%** (0.67x) |
| **THERMOS** | **3393.63** | [3364.53, 3422.74] | **98.4%** (0.98x) |

### Capacity Tier 25.0% (14075.3 MB HBM Budget)

| Strategy | Mean FOM (z/s) | 95% Confidence Interval | Relative FOM vs HBM |
| :--- | :--- | :--- | :--- |
| **FIRST-TOUCH** | **2270.08** | [2199.51, 2340.66] | **65.8%** (0.66x) |
| **KNAPSACK** | **3415.04** | [3379.58, 3450.50] | **99.0%** (0.99x) |
| **HOTSET** | **3392.73** | [3381.08, 3404.39] | **98.3%** (0.98x) |
| **THERMOS** | **3452.79** | [3448.57, 3457.01] | **100.1%** (1.00x) |

### Capacity Tier 50.0% (28150.6 MB HBM Budget)

| Strategy | Mean FOM (z/s) | 95% Confidence Interval | Relative FOM vs HBM |
| :--- | :--- | :--- | :--- |
| **FIRST-TOUCH** | **3390.22** | [3375.71, 3404.73] | **98.3%** (0.98x) |
| **KNAPSACK** | **3370.23** | [3361.37, 3379.10] | **97.7%** (0.98x) |
| **HOTSET** | **3423.26** | [3407.59, 3438.92] | **99.2%** (0.99x) |
| **THERMOS** | **3386.53** | [3376.85, 3396.22] | **98.2%** (0.98x) |
