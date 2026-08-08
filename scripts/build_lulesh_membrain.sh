#!/bin/bash
# ==============================================================================
# Script to build a dedicated MemBrain-instrumented binary of LULESH 2.0.
#
# Description:
#   Builds the MemBrain runtime infrastructure if needed, then instruments
#   LULESH using Intel Compiler (icpx / icx) with flags -O3 -ffast-math -xHost.
#   This build is completely isolated in LULESH/build_membrain/ and does NOT
#   touch or affect standard unguided LULESH builds in LULESH/build_baseline/.
#
# Usage:
#   ./scripts/build_lulesh_membrain.sh
#
# Environment Variables:
#   LULESH_DIR - Path to LULESH repository (Default: ../LULESH)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"

MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

if [ ! -f "${MEMBRAIN_RT}" ]; then
    echo "[MemBrain Build] Building MemBrain runtime infrastructure..."
    mkdir -p "${MEMBRAIN_ROOT}/build"
    cd "${MEMBRAIN_ROOT}/build"
    cmake ..
    make -j $(nproc)
fi

# Source Intel oneAPI environment if available
if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || source /opt/intel/oneapi/setvars.sh
fi

BUILD_MEMBRAIN_DIR="${LULESH_DIR}/build_membrain"
echo "[MemBrain Build] Building MemBrain LULESH with Intel icpx in: ${BUILD_MEMBRAIN_DIR}"

rm -rf "${BUILD_MEMBRAIN_DIR}"
mkdir -p "${BUILD_MEMBRAIN_DIR}"
cd "${BUILD_MEMBRAIN_DIR}"

CC=icx CXX=icpx cmake "${LULESH_DIR}" \
    -DCMAKE_CXX_FLAGS="-O3 -ffast-math -xHost -g -I${MEMBRAIN_ROOT}/runtime" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${MEMBRAIN_ROOT}/build/runtime -lmembrain_rt -Wl,-rpath,${MEMBRAIN_ROOT}/build/runtime" \
    -DWITH_MPI=Off \
    -DWITH_OPENMP=On

make -j $(nproc)

if [ -f "${BUILD_MEMBRAIN_DIR}/lulesh2.0" ]; then
    mv "${BUILD_MEMBRAIN_DIR}/lulesh2.0" "${BUILD_MEMBRAIN_DIR}/lulesh2.0_membrain"
    echo "[SUCCESS] MemBrain-instrumented LULESH binary created at:"
    echo "         ${BUILD_MEMBRAIN_DIR}/lulesh2.0_membrain"
else
    echo "[ERROR] Compilation failed to produce binary."
    exit 1
fi
