#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"
BASELINE_BIN="${LULESH_DIR}/build_intel/lulesh2.0"

MODE="${1:-ddr}"
shift 1 2>/dev/null || true

LULESH_ARGS=("$@")
if [ ${#LULESH_ARGS[@]} -eq 0 ]; then
    LULESH_ARGS=(-s 420 -i 5 -r 11 -b 0 -c 64 -p)
fi

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || source /opt/intel/oneapi/setvars.sh
fi

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
