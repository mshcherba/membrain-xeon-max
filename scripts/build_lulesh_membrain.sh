#!/bin/bash
# ==============================================================================
# Script to build a dedicated MemBrain-instrumented binary of LULESH 2.0.
#
# Description:
#   Builds the MemBrain LLVM pass and runtime if needed, then instruments
#   LULESH using clang++ -fpass-plugin=MemBrainPass.so.
#   This build is completely isolated in LULESH/build_membrain/ and does NOT
#   touch or affect standard unguided LULESH builds in LULESH/build_intel/.
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

MEMBRAIN_PASS="${MEMBRAIN_ROOT}/build/llvm-pass/MemBrainPass.so"
MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

if [ ! -f "${MEMBRAIN_PASS}" ] || [ ! -f "${MEMBRAIN_RT}" ]; then
    echo "[MemBrain Build] Building MemBrain infrastructure first..."
    mkdir -p "${MEMBRAIN_ROOT}/build"
    cd "${MEMBRAIN_ROOT}/build"
    cmake ..
    make -j $(nproc)
fi

# Use clang++ for LLVM Pass compatibility
COMPILER="clang++"

BUILD_MEMBRAIN_DIR="${LULESH_DIR}/build_membrain"
echo "[MemBrain Build] Creating isolated build directory: ${BUILD_MEMBRAIN_DIR}"
rm -rf "${BUILD_MEMBRAIN_DIR}"
rm -f "${MEMBRAIN_ROOT}/build/allocation_sites.json" allocation_sites.json
mkdir -p "${BUILD_MEMBRAIN_DIR}"
cd "${BUILD_MEMBRAIN_DIR}"

cmake "${LULESH_DIR}" \
    -DCMAKE_CXX_COMPILER="${COMPILER}" \
    -DCMAKE_CXX_FLAGS="-O3 -ffast-math -march=native -g -fpass-plugin=${MEMBRAIN_PASS} -I${MEMBRAIN_ROOT}/runtime" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${MEMBRAIN_ROOT}/build/runtime -lmembrain_rt -Wl,-rpath,${MEMBRAIN_ROOT}/build/runtime" \
    -DWITH_MPI=Off \
    -DWITH_OPENMP=On

make -j $(nproc)

# Merge per-module allocation_sites_*.json into single allocation_sites.json
python3 "${MEMBRAIN_ROOT}/scripts/merge_allocation_sites.py" --dir "${BUILD_MEMBRAIN_DIR}" --output "${BUILD_MEMBRAIN_DIR}/allocation_sites.json"

if [ -f "${BUILD_MEMBRAIN_DIR}/lulesh2.0" ]; then
    mv "${BUILD_MEMBRAIN_DIR}/lulesh2.0" "${BUILD_MEMBRAIN_DIR}/lulesh2.0_membrain"
    echo "[SUCCESS] MemBrain-instrumented LULESH binary created at:"
    echo "         ${BUILD_MEMBRAIN_DIR}/lulesh2.0_membrain"
    echo "         (Standard baseline builds in build_intel/ remain completely untouched)"
else
    echo "[ERROR] Compilation failed to produce binary."
    exit 1
fi
