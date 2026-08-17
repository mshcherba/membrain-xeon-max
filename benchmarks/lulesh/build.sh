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

# Check for Intel compiler and run dependency installer if missing
if ! command -v icpx >/dev/null 2>&1 && [ ! -f /opt/intel/oneapi/setvars.sh ]; then
    if [ "$EUID" -eq 0 ]; then
        echo "[INFO] Intel oneAPI compiler not found. Running dependency installation script..."
        bash "${MEMBRAIN_ROOT}/scripts/install_dependencies.sh"
    else
        echo "[ERROR] Intel oneAPI compiler not found. Please install dependencies first:"
        echo "       sudo bash ${MEMBRAIN_ROOT}/scripts/install_dependencies.sh"
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
echo "[INFO] Building LULESH with 2-Stage Pipeline (MemBrain LLVM Pass + Intel icpx)..."

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

CLANG_CXX="${CLANG_CXX:-clang++}"
if ! command -v "${CLANG_CXX}" >/dev/null 2>&1; then
    echo "[ERROR] C++ Clang compiler ('${CLANG_CXX}') not found."
    exit 1
fi

PASS_SO="${MEMBRAIN_ROOT}/build/llvm-pass/MemBrainPass.so"
RT_DIR="${MEMBRAIN_ROOT}/build/runtime"
INC_DIR="${MEMBRAIN_ROOT}/runtime"

export MEMBRAIN_SITES_DIR="${BUILD_DIR}"

echo " -> Stage A: LLVM Pass Transformation & Parallel Object Compilation..."
for src in "${LULESH_DIR}"/*.cc; do
    [ -f "${src}" ] || continue
    (
        base=$(basename "${src}" .cc)
        echo "    Processing $(basename "${src}")..."
        "${CLANG_CXX}" -S -emit-llvm -O3 -ffast-math -g -fopenmp -DUSE_MPI=0 \
            -fpass-plugin="${PASS_SO}" \
            -I"${INC_DIR}" \
            "${src}" -o "${base}_transformed.ll"
            
        icpx -c -O3 -ffast-math -xHost -g -fiopenmp "${base}_transformed.ll" -o "${base}.o"
    ) &
done
wait

echo " -> Merging Allocation Sites Metadata into allocation_sites.json..."
python3 "${MEMBRAIN_ROOT}/scripts/merge_allocation_sites.py" --dir "${BUILD_DIR}" --output "${BUILD_DIR}/allocation_sites.json"

echo " -> Stage B: Intel icpx Native Compilation & Linking..."
icpx -O3 -ffast-math -xHost -g -fiopenmp *.o \
    -L"${RT_DIR}" -lmembrain_rt -Wl,-rpath,"${RT_DIR}" \
    -o "${BUILD_DIR}/lulesh2.0"

if [ -f "${BUILD_DIR}/lulesh2.0" ]; then
    echo "[SUCCESS] MemBrain LULESH binary successfully built at:"
    echo "         ${BUILD_DIR}/lulesh2.0"
else
    echo "[ERROR] Build failed. Binary not found."
    exit 1
fi
