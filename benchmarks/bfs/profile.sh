#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BFS_DIR="${BFS_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/Graph500-BFS}"
BFS_BIN="${BFS_DIR}/mpi/runnable"
SITES_JSON="${BFS_DIR}/mpi/allocation_sites.json"
MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

[ -f "${BFS_BIN}"     ] || { echo "ERROR: BFS_BIN not found at '${BFS_BIN}'" >&2; exit 1; }
[ -f "${SITES_JSON}"  ] || { echo "ERROR: SITES_JSON not found at '${SITES_JSON}'" >&2; exit 1; }
[ -f "${MEMBRAIN_RT}" ] || { echo "ERROR: MEMBRAIN_RT not found at '${MEMBRAIN_RT}'" >&2; exit 1; }

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
fi

# Load common OpenMP & affinity environment settings
source "${MEMBRAIN_ROOT}/benchmarks/common_env.sh"

BFS_ARGS=("$@")
if [ ${#BFS_ARGS[@]} -eq 0 ]; then
    BFS_ARGS=(25 -A -C -n 64)
fi

echo "=============================================================================="
echo " Starting MemBrain PEBS Profiling Run for Graph500-BFS"
echo " Problem Parameters: ${BFS_ARGS[*]}"
echo " OpenMP Threads:     ${OMP_NUM_THREADS}"
echo " KMP Affinity:       ${KMP_AFFINITY}"
echo "=============================================================================="

/usr/bin/time -v python3 "${MEMBRAIN_ROOT}/profiler/pebs_profiler.py" \
    --sites "${SITES_JSON}" \
    --runtime "${MEMBRAIN_RT}" \
    --output "${SCRIPT_DIR}/profile_data.json" \
    numactl --cpunodebind=0 "${BFS_BIN}" "${BFS_ARGS[@]}"

echo "=============================================================================="
echo " Profiling Run Completed Successfully"
echo "=============================================================================="
