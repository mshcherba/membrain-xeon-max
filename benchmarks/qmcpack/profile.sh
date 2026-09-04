#!/bin/bash
# ==============================================================================
# MemBrain PEBS Profiler Wrapper for QMCPACK.
#
# Usage:
#   ./profile.sh [QMC_ARGS...]
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
QMCPACK_DIR="${QMCPACK_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/qmcpack}"
QMCPACK_BIN="${QMCPACK_DIR}/build_membrain/bin/qmcpack"
SITES_JSON="${QMCPACK_DIR}/build_membrain/allocation_sites.json"
MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

[ -f "${QMCPACK_BIN}" ] || { echo "ERROR: QMCPACK_BIN not found at '${QMCPACK_BIN}'" >&2; exit 1; }
[ -f "${SITES_JSON}" ] || { echo "ERROR: SITES_JSON not found at '${SITES_JSON}'" >&2; exit 1; }
[ -f "${MEMBRAIN_RT}" ] || { echo "ERROR: MEMBRAIN_RT not found at '${MEMBRAIN_RT}'" >&2; exit 1; }
[ -f "${SCRIPT_DIR}/Ni.opt.xml" ] || { echo "ERROR: Ni.opt.xml not found at '${SCRIPT_DIR}/Ni.opt.xml'" >&2; exit 1; }
[ -f "${SCRIPT_DIR}/O.xml" ] || { echo "ERROR: O.xml not found at '${SCRIPT_DIR}/O.xml'" >&2; exit 1; }
[ -f "${SCRIPT_DIR}/NiO-fcc-supertwist111-supershift000-S64.h5" ] || {
    echo "ERROR: Wavefunction file '${SCRIPT_DIR}/NiO-fcc-supertwist111-supershift000-S64.h5' not found!" >&2
    echo "       Please set export QMC_DATA_URL='<url>' and rebuild or place the file directly in ${SCRIPT_DIR}." >&2
    exit 1
}

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
fi

# Load common OpenMP & affinity environment settings
source "${MEMBRAIN_ROOT}/benchmarks/common_env.sh"

cd "${SCRIPT_DIR}"

QMC_ARGS=("$@")
if [ ${#QMC_ARGS[@]} -eq 0 ]; then
    QMC_ARGS=("${SCRIPT_DIR}/NiO-fcc-S64.xml")
fi

echo "=============================================================================="
echo " Starting MemBrain PEBS Profiling Run for QMCPACK"
echo " Problem Input:      ${QMC_ARGS[*]}"
echo " OpenMP Threads:     ${OMP_NUM_THREADS}"
echo " KMP Affinity:       ${KMP_AFFINITY}"
echo "=============================================================================="

/usr/bin/time -v python3 "${MEMBRAIN_ROOT}/profiler/pebs_profiler.py" \
    --sites "${SITES_JSON}" \
    --runtime "${MEMBRAIN_RT}" \
    --output "${SCRIPT_DIR}/profile_data.json" \
    numactl --cpunodebind=0 "${QMCPACK_BIN}" "${QMC_ARGS[@]}"

echo "=============================================================================="
echo " Profiling Run Completed Successfully"
echo "=============================================================================="
