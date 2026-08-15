#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"
LULESH_BIN="${LULESH_DIR}/build/lulesh2.0"

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || true
fi

export MEMBRAIN_GUIDANCE_PATH="${MEMBRAIN_ROOT}/guidance/ddr_only.json"
export MEMBRAIN_VERBOSE=1
export MEMBRAIN_TRACE=0
export OMP_NUM_THREADS=32
export KMP_AFFINITY="granularity=fine,compact,1,0"

numactl --cpunodebind=0 "${LULESH_BIN}" -s 50 -i 1 -r 11 -b 0 -c 64 -p 2>&1
