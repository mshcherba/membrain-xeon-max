#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || true
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"
export KMP_AFFINITY="${KMP_AFFINITY:-granularity=fine,compact,1,0}"

LULESH_BIN="${LULESH_BIN:-${LULESH_DIR}/build/lulesh2.0}"
if [ ! -f "${LULESH_BIN}" ]; then
    echo "ERROR: LULESH binary not found at '${LULESH_BIN}'. Set LULESH_BIN env var." >&2
    exit 1
fi

# --membind=0 forces all memory onto DDR (NUMA node 0) for a pure DDR baseline measurement
/usr/bin/time -v numactl --cpunodebind=0 --membind=0 "${LULESH_BIN}" -s 400 -i 5 -r 11 -b 0 -c 64 -p 2>&1
