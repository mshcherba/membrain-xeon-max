#!/bin/bash
# ==============================================================================
# Unified Build Script for LULESH 2.0 with Intel Compiler & MemBrain Integration.
#
# Description:
#   Automatically sets up Intel oneAPI compiler (icpx / icx), compiles the
#   MemBrain runtime library, clones LULESH if missing, and compiles the
#   unified LULESH 2.0 binary into LULESH/build/lulesh2.0 using Intel compiler
#   flags (-O3 -ffast-math -xHost) linked against libmembrain_rt.so.
#   This single binary serves all execution modes: pure DDR5, pure HBM2e, and MemBrain.
#
# Usage:
#   ./scripts/build_lulesh.sh
#
# Environment Variables:
#   LULESH_DIR - Path to LULESH repository (Default: ../LULESH)
#   WITH_MPI   - Set to 'On' to build with MPI (Default: Off)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"
MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

# Check for Intel compiler and run dependency installer if missing
if ! command -v icpx >/dev/null 2>&1 && [ ! -f /opt/intel/oneapi/setvars.sh ]; then
    if [ "$EUID" -eq 0 ]; then
        echo "[INFO] Intel oneAPI compiler not found. Running dependency installation script..."
        bash "${SCRIPT_DIR}/install_dependencies.sh"
    else
        echo "[ERROR] Intel oneAPI compiler not found. Please install dependencies first:"
        echo "       sudo bash ${SCRIPT_DIR}/install_dependencies.sh"
        exit 1
    fi
fi

# Source Intel oneAPI environment if setvars.sh exists and icpx is not yet in PATH
if ! command -v icpx >/dev/null 2>&1 && [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
fi

if ! command -v icpx >/dev/null 2>&1; then
    echo "[ERROR] Intel C++ compiler (icpx) is not available in PATH."
    exit 1
fi

# 1. Build MemBrain Runtime Library if missing
if [ ! -f "${MEMBRAIN_RT}" ]; then
    echo "[INFO] Building MemBrain runtime infrastructure..."
    mkdir -p "${MEMBRAIN_ROOT}/build"
    cd "${MEMBRAIN_ROOT}/build"
    cmake ..
    make -j $(nproc)
fi

# 2. Clone LULESH if directory does not exist
if [ ! -d "${LULESH_DIR}" ]; then
    echo "[INFO] LULESH repository not found at ${LULESH_DIR}. Cloning from GitHub..."
    git clone https://github.com/LLNL/LULESH.git "${LULESH_DIR}"
fi

BUILD_DIR="${LULESH_DIR}/build"
echo "[INFO] Building unified LULESH binary with Intel icpx in: ${BUILD_DIR}"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

CC=icx CXX=icpx cmake "${LULESH_DIR}" \
    -DCMAKE_CXX_FLAGS="-O3 -ffast-math -xHost -g -I${MEMBRAIN_ROOT}/runtime" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${MEMBRAIN_ROOT}/build/runtime -lmembrain_rt -Wl,-rpath,${MEMBRAIN_ROOT}/build/runtime" \
    -DWITH_MPI="${WITH_MPI:-Off}" \
    -DWITH_OPENMP=On

make -j $(nproc)

if [ -f "${BUILD_DIR}/lulesh2.0" ]; then
    echo "[SUCCESS] Unified LULESH binary successfully built at:"
    echo "         ${BUILD_DIR}/lulesh2.0"
else
    echo "[ERROR] Build failed. Binary not found."
    exit 1
fi
