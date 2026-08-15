#!/bin/bash
# ==============================================================================
# Repeated Benchmark Execution Script for LULESH on Intel Xeon Max.
#
# Description:
#   Executes run_lulesh_single.sh multiple times (default: 5 runs) in the
#   specified memory mode (ddr or hbm) with optional LULESH arguments.
#
# Usage:
#   ./scripts/run_lulesh_repeats.sh [NUM_RUNS] [MODE] [LULESH_ARGS...]
#
# Parameters:
#   NUM_RUNS - Number of benchmark iterations to execute (Default: 5)
#   MODE     - Memory binding mode: ddr (node 0) or hbm (node 2) (Default: ddr)
#
# LULESH_ARGS:
#   Optional CLI parameters passed through to LULESH.
#
# Examples:
#   ./scripts/run_lulesh_repeats.sh
#   ./scripts/run_lulesh_repeats.sh 10 hbm
#   ./scripts/run_lulesh_repeats.sh 5 hbm -s 30 -i 5
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

NUM_RUNS="${1:-5}"
MODE="${2:-ddr}"

if [ $# -ge 2 ]; then
    shift 2
elif [ $# -eq 1 ]; then
    shift 1
fi

LULESH_ARGS=("$@")

# Validate NUM_RUNS is a positive integer
if ! [[ "${NUM_RUNS}" =~ ^[0-9]+$ ]] || [ "${NUM_RUNS}" -le 0 ]; then
    echo "[ERROR] NUM_RUNS must be a positive integer. Got: '${NUM_RUNS}'"
    exit 1
fi

SINGLE_SCRIPT="${SCRIPT_DIR}/run_single.sh"

if [ ! -f "${SINGLE_SCRIPT}" ]; then
    echo "[ERROR] Single run script not found at ${SINGLE_SCRIPT}"
    exit 1
fi

echo "================================================================="
echo " Starting Repeated Benchmark Execution"
echo "   Runs: ${NUM_RUNS}"
echo "   Mode: ${MODE}"
if [ ${#LULESH_ARGS[@]} -gt 0 ]; then
    echo "   Args: ${LULESH_ARGS[*]}"
fi
echo "================================================================="

for (( i=1; i<=NUM_RUNS; i++ )); do
    echo ""
    echo "[Run ${i}/${NUM_RUNS}] Executing ${SINGLE_SCRIPT} ${MODE} ${LULESH_ARGS[*]}"
    echo "-----------------------------------------------------------------"
    bash "${SINGLE_SCRIPT}" "${MODE}" "${LULESH_ARGS[@]}"
done

echo ""
echo "================================================================="
echo " Completed all ${NUM_RUNS} benchmark runs successfully."
echo "================================================================="
