#!/bin/bash
# ==============================================================================
# Script for a single benchmark run of LULESH on Intel Xeon Max (Socket 0).
#
# Description:
#   Executes a single benchmark run of baseline LULESH on Socket 0 CPU,
#   binding memory to either DDR5 (node 0) or HBM2e (node 2).
#
# Usage:
#   ./scripts/run_lulesh_single.sh [MODE] [LULESH_ARGS...]
#
# Modes:
#   ddr | 0 - Binds memory to DDR5 (numactl --membind 0)
#   hbm | 2 - Binds memory to HBM2e (numactl --membind 2)
#
# LULESH_ARGS:
#   Optional CLI parameters passed to LULESH.
#   Default: -s 400 -i 5 -r 11 -b 0 -c 64 -p
#
# Environment Variables:
#   LULESH_DIR - Path to LULESH repository (Default: ../LULESH)
#
# Examples:
#   ./scripts/run_lulesh_single.sh ddr
#   ./scripts/run_lulesh_single.sh hbm -s 30 -i 5
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"
BASELINE_BIN="${LULESH_DIR}/build/lulesh2.0"

MODE="${1:-ddr}"
shift 1 2>/dev/null || true

LULESH_ARGS=("$@")
if [ ${#LULESH_ARGS[@]} -eq 0 ]; then
    LULESH_ARGS=(-s 400 -i 5 -r 11 -b 0 -c 64 -p)
fi

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || source /opt/intel/oneapi/setvars.sh
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"
export KMP_AFFINITY="${KMP_AFFINITY:-granularity=fine,compact,1,0}"

case "${MODE}" in
    ddr|0)
        MEM_NODE="0"
        ;;
    hbm|2)
        MEM_NODE="2"
        ;;
    *)
        echo "[ERROR] Unknown mode: '${MODE}'. Valid options: ddr (0), hbm (2)."
        exit 1
        ;;
esac

numactl --cpunodebind=0 --membind="${MEM_NODE}" "${BASELINE_BIN}" "${LULESH_ARGS[@]}"
