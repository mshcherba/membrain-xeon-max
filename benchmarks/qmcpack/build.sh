#!/bin/bash
# ==============================================================================
# Unified Build Script for QMCPACK with LLVM Pass & Intel oneAPI Integration.
#
# Description:
#   Automatically compiles the MemBrain LLVM Pass and Runtime library,
#   clones QMCPACK if missing, and executes the build pipeline:
#     Stage A: LLVM Pass Transformation & Compilation with Clang
#              (clang++ -c -O3 -ffast-math -fopenmp -fpass-plugin=MemBrainPass.so)
#              generating per-TU allocation sites in sites/
#     Stage B: Intel oneAPI Integration (MKL, MPI, OpenMP) & libmembrain_rt.so
#     Stage C: Merging Allocation Sites Metadata into allocation_sites.json
#   Outputs the unified MemBrain QMCPACK binary at qmcpack/build_membrain/bin/qmcpack.
#
# Usage:
#   ./build.sh
#
# Environment Variables:
#   QMCPACK_DIR   - Path to QMCPACK repository (Default: ../qmcpack)
#   CLANG_CXX     - Path/binary of Clang C++ compiler (Default: clang++)
#   INTEL_MPI_CXX - Path/binary of Intel oneAPI MPI C++ compiler (Default: mpiicpx)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
QMCPACK_DIR="${QMCPACK_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/qmcpack}"
BUILD_DIR="${QMCPACK_DIR}/build_membrain"
PASS_SO="${MEMBRAIN_ROOT}/build/llvm-pass/MemBrainPass.so"
RT_DIR="${MEMBRAIN_ROOT}/build/runtime"
INC_DIR="${MEMBRAIN_ROOT}/runtime"

# Check for Intel oneAPI environment
if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
fi

# 1. Build MemBrain Runtime and LLVM Pass if missing
if [ ! -f "${PASS_SO}" ] || [ ! -f "${RT_DIR}/libmembrain_rt.so" ]; then
    echo "[INFO] Building MemBrain runtime and LLVM pass..."
    mkdir -p "${MEMBRAIN_ROOT}/build"
    cd "${MEMBRAIN_ROOT}/build"
    cmake ..
    make -j $(nproc)
fi

# 2. Clone QMCPACK if directory does not exist
if [ ! -d "${QMCPACK_DIR}" ]; then
    echo "[INFO] QMCPACK repository not found at ${QMCPACK_DIR}. Cloning from GitHub..."
    git clone https://github.com/QMCPACK/qmcpack.git "${QMCPACK_DIR}"
fi

# 3. Setup relative symlinks for benchmark input data
if [ -d "${MEMBRAIN_ROOT}/../qmc_data/NiO" ]; then
    ln -sf ../../../qmc_data/NiO/NiO-fcc-supertwist111-supershift000-S64.h5 "${SCRIPT_DIR}/NiO-fcc-supertwist111-supershift000-S64.h5"
    ln -sf ../../../qmc_data/NiO/Ni.opt.xml "${SCRIPT_DIR}/Ni.opt.xml"
    ln -sf ../../../qmc_data/NiO/O.xml "${SCRIPT_DIR}/O.xml"
fi

mkdir -p "${BUILD_DIR}/sites"
cd "${BUILD_DIR}"

export MEMBRAIN_SITES_DIR="${BUILD_DIR}/sites"
export MEMBRAIN_CLONE_DEPTH="${MEMBRAIN_CLONE_DEPTH:-4}"

CLANG_CXX="${CLANG_CXX:-clang++}"
CLANG_C="$(echo "${CLANG_CXX}" | sed 's/++$//')"
INTEL_MPI_CXX="${INTEL_MPI_CXX:-mpiicpx}"

echo "[INFO] Building QMCPACK with MemBrain LLVM Pass and Intel oneAPI Compiler (${INTEL_MPI_CXX})..."

cmake -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="${CLANG_C}" \
  -DCMAKE_CXX_COMPILER="${CLANG_CXX}" \
  -DCMAKE_CXX_LINK_EXECUTABLE="${INTEL_MPI_CXX} <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>" \
  -DCMAKE_CXX_FLAGS="-O3 -ffast-math -fopenmp -fpass-plugin=${PASS_SO} -I${INC_DIR} -I/opt/intel/oneapi/mpi/latest/include" \
  -DCMAKE_C_FLAGS="-O3 -ffast-math -fopenmp -fpass-plugin=${PASS_SO} -I${INC_DIR} -I/opt/intel/oneapi/mpi/latest/include" \
  -DCMAKE_EXE_LINKER_FLAGS="-L${RT_DIR} -lmembrain_rt -Wl,-rpath,${RT_DIR} -L/opt/intel/oneapi/umf/latest/lib -lumf -Wl,-rpath,/opt/intel/oneapi/umf/latest/lib -lnuma -lhwloc" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L${RT_DIR} -lmembrain_rt -Wl,-rpath,${RT_DIR} -L/opt/intel/oneapi/umf/latest/lib -lumf -Wl,-rpath,/opt/intel/oneapi/umf/latest/lib -lnuma -lhwloc" \
  -DQMC_MPI=ON \
  -DQMC_OMP=ON \
  -DQMC_COMPLEX=OFF \
  -DENABLE_TIMERS=ON \
  "${QMCPACK_DIR}"

ninja -j 16 qmcpack

echo " -> Merging Allocation Sites Metadata into allocation_sites.json..."
python3 "${MEMBRAIN_ROOT}/scripts/merge_allocation_sites.py" \
    --dir "${BUILD_DIR}/sites" \
    --output "${BUILD_DIR}/allocation_sites.json"

if [ -f "${BUILD_DIR}/bin/qmcpack" ] && [ -s "${BUILD_DIR}/allocation_sites.json" ]; then
    echo "[SUCCESS] MemBrain QMCPACK binary successfully built at:"
    echo "         ${BUILD_DIR}/bin/qmcpack"
else
    echo "[ERROR] Build failed. Binary or allocation sites metadata missing."
    exit 1
fi
