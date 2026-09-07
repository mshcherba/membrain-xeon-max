#!/bin/bash
# ==============================================================================
# Unified Build Script for LULESH 2.0 with Intel Compiler & MemBrain Integration.
#
# Description:
#   Automatically compiles the MemBrain LLVM Pass and Runtime library, clones
#   LULESH 2.0 if missing, and executes the 2-stage build pipeline:
#     Stage A: LLVM Pass Transformation (clang++ -emit-llvm -fpass-plugin=MemBrainPass.so)
#              enabling call-path function cloning and malloc/free interposition.
#     Stage B: Intel icpx Native Compilation & Linking (-O3 -ffast-math -xHost -fiopenmp)
#              linked against libmembrain_rt.so with UMF Scalable Memory Pools.
#   Outputs the unified MemBrain LULESH binary at LULESH/build/lulesh2.0.
#
# Usage:
#   ./build.sh
#
# Environment Variables:
#   LULESH_DIR - Path to LULESH repository (Default: ../LULESH)
#   CLANG_CXX  - Path/binary of Clang C++ compiler (Default: clang++)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"
MEMBRAIN_RT="${MEMBRAIN_ROOT}/build/runtime/libmembrain_rt.so"

CLANG_CXX="${CLANG_CXX:-clang++}"

# Source Intel oneAPI environment if available
if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
fi

# Check for required tools and run dependency installer if missing
if ! command -v cmake >/dev/null 2>&1 || \
   ! command -v make >/dev/null 2>&1 || \
   ! command -v "${CLANG_CXX}" >/dev/null 2>&1 || \
   ! command -v icpx >/dev/null 2>&1 || \
   [ ! -f /opt/intel/oneapi/setvars.sh ]; then
    echo "[INFO] Missing required build dependencies. Running dependency installation script..."
    if [ "$EUID" -eq 0 ]; then
        bash "${MEMBRAIN_ROOT}/scripts/install_dependencies.sh"
    elif command -v sudo >/dev/null 2>&1; then
        sudo bash "${MEMBRAIN_ROOT}/scripts/install_dependencies.sh"
    else
        echo "[ERROR] Missing required dependencies. Please run as root or install first:" >&2
        echo "       sudo bash ${MEMBRAIN_ROOT}/scripts/install_dependencies.sh" >&2
        exit 1
    fi

    # Re-source oneAPI environment after installation
    if [ -f /opt/intel/oneapi/setvars.sh ]; then
        source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
    fi
fi

if ! command -v icpx >/dev/null 2>&1; then
    echo "[ERROR] Intel C++ compiler (icpx) is not available in PATH." >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] cmake is not available in PATH." >&2
    exit 1
fi
if ! command -v "${CLANG_CXX}" >/dev/null 2>&1; then
    echo "[ERROR] Clang C++ compiler (${CLANG_CXX}) is not available in PATH." >&2
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
echo "[INFO] Building LULESH with Whole-Program Pipeline (llvm-link + MemBrain LLVM Pass + Intel icpx)..."

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

CLANG_CXX="${CLANG_CXX:-clang++}"
if ! command -v "${CLANG_CXX}" >/dev/null 2>&1; then
    echo "[ERROR] C++ Clang compiler ('${CLANG_CXX}') not found."
    exit 1
fi

if ! command -v llvm-link >/dev/null 2>&1; then
    echo "[ERROR] Required tool 'llvm-link' not found in PATH."
    exit 1
fi

PASS_SO="${MEMBRAIN_ROOT}/build/llvm-pass/MemBrainPass.so"
RT_DIR="${MEMBRAIN_ROOT}/build/runtime"
INC_DIR="${MEMBRAIN_ROOT}/runtime"

echo " -> Stage A1: Emitting LLVM Bitcode for each translation unit..."
for src in "${LULESH_DIR}"/*.cc; do
    [ -f "${src}" ] || continue
    (
        base=$(basename "${src}" .cc)
        echo "    Compiling $(basename "${src}") to LLVM bitcode..."
        "${CLANG_CXX}" -emit-llvm -c -O3 -ffast-math -g -fopenmp -DUSE_MPI=0 \
            -I"${INC_DIR}" \
            "${src}" -o "${base}.bc"
    ) &
done
wait

echo " -> Stage A2: Linking Whole-Program Bitcode with llvm-link..."
llvm-link *.bc -o lulesh_linked.bc

echo " -> Stage A3: Whole-Program MemBrain Transformation (Optimization + Cloning + Tagging)..."
export MEMBRAIN_SITES_FILE="${BUILD_DIR}/allocation_sites.json"
export MEMBRAIN_CLONE_DEPTH="${MEMBRAIN_CLONE_DEPTH:-4}"
"${CLANG_CXX}" -S -emit-llvm -O3 -ffast-math -g -fopenmp \
    -fpass-plugin="${PASS_SO}" \
    lulesh_linked.bc -o lulesh_transformed.ll

if [ ! -s "${BUILD_DIR}/allocation_sites.json" ]; then
    echo "[ERROR] Allocation sites metadata '${BUILD_DIR}/allocation_sites.json' was not generated or is empty."
    exit 1
fi

echo " -> Stage B: Intel icpx Native Compilation & Linking..."
icpx -c -O3 -ffast-math -xHost -g -fiopenmp lulesh_transformed.ll -o lulesh.o
icpx -O3 -ffast-math -xHost -g -fiopenmp lulesh.o \
    -L"${RT_DIR}" -lmembrain_rt -Wl,-rpath,"${RT_DIR}" \
    -o "${BUILD_DIR}/lulesh2.0"

if [ -f "${BUILD_DIR}/lulesh2.0" ]; then
    echo "[SUCCESS] MemBrain LULESH binary successfully built at:"
    echo "         ${BUILD_DIR}/lulesh2.0"
else
    echo "[ERROR] Build failed. Binary not found."
    exit 1
fi
