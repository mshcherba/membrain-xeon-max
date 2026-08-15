#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"
LULESH_BIN="${LULESH_DIR}/build/lulesh2.0"
SITES_JSON="${LULESH_DIR}/build/allocation_sites.json"
MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

[ -f "${LULESH_BIN}"  ] || { echo "ERROR: LULESH_BIN not found at '${LULESH_BIN}'" >&2; exit 1; }
[ -f "${SITES_JSON}"  ] || { echo "ERROR: SITES_JSON not found at '${SITES_JSON}'" >&2; exit 1; }
[ -f "${MEMBRAIN_RT}" ] || { echo "ERROR: MEMBRAIN_RT not found at '${MEMBRAIN_RT}'" >&2; exit 1; }

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || true
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"
export KMP_AFFINITY="${KMP_AFFINITY:-granularity=fine,compact,1,0}"

LULESH_ARGS=("$@")
if [ ${#LULESH_ARGS[@]} -eq 0 ]; then
    LULESH_ARGS=(-s 400 -i 5 -r 11 -b 0 -c 64 -p)
fi

echo "=============================================================================="
echo " Starting MemBrain PEBS Profiling Run"
echo " Problem Parameters: ${LULESH_ARGS[*]}"
echo " OpenMP Threads:     ${OMP_NUM_THREADS}"
echo " KMP Affinity:       ${KMP_AFFINITY}"
echo "=============================================================================="

/usr/bin/time -v python3 "${MEMBRAIN_ROOT}/profiler/pebs_profiler.py" \
    --sites "${SITES_JSON}" \
    --runtime "${MEMBRAIN_RT}" \
    --output "${MEMBRAIN_ROOT}/profile_data.json" \
    numactl --cpunodebind=0 "${LULESH_BIN}" "${LULESH_ARGS[@]}"

echo "=============================================================================="
echo " Profiling Run Completed Successfully"
echo "=============================================================================="
