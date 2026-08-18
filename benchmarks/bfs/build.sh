#!/bin/bash
# ==============================================================================
# Unified Build Script for Graph500-BFS with Intel Compiler & MemBrain Integration.
#
# Description:
#   Automatically compiles the MemBrain LLVM Pass and Runtime library, clones
#   Graph500-BFS if missing, and executes the 2-stage build pipeline:
#     Stage A: LLVM Pass Transformation & Object Compilation
#              (clang++ -c -O3 -ffast-math -g -fopenmp -fpass-plugin=MemBrainPass.so)
#              enabling call-path function cloning and malloc/free interposition.
#     Stage B: Intel mpiicpx Native Linking (-O3 -ffast-math -xHost -fiopenmp)
#              linked against Intel MPI and libmembrain_rt.so.
#   Outputs the unified MemBrain Graph500-BFS binary at Graph500-BFS/mpi/runnable.
#
# Usage:
#   ./build.sh
#
# Environment Variables:
#   BFS_DIR   - Path to Graph500-BFS repository (Default: ../Graph500-BFS)
#   CLANG_CXX - Path/binary of Clang C++ compiler (Default: clang++)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BFS_DIR="${BFS_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/Graph500-BFS}"
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

# Source Intel oneAPI environment if setvars.sh exists
if [ -f /opt/intel/oneapi/setvars.sh ]; then
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

# 2. Clone Graph500-BFS if directory does not exist
if [ ! -d "${BFS_DIR}" ]; then
    echo "[INFO] Graph500-BFS repository not found at ${BFS_DIR}. Cloning from GitHub..."
    git clone https://github.com/RIKEN-RCCS/Graph500-BFS.git "${BFS_DIR}"
fi

BUILD_DIR="${BFS_DIR}/mpi"
echo "[INFO] Building Graph500-BFS with 2-Stage Pipeline (MemBrain LLVM Pass + Intel icpx)..."

CLANG_CXX="${CLANG_CXX:-clang++}"
if ! command -v "${CLANG_CXX}" >/dev/null 2>&1; then
    echo "[ERROR] C++ Clang compiler ('${CLANG_CXX}') not found."
    exit 1
fi

PASS_SO="${MEMBRAIN_ROOT}/build/llvm-pass/MemBrainPass.so"
RT_DIR="${MEMBRAIN_ROOT}/build/runtime"
INC_DIR="${MEMBRAIN_ROOT}/runtime"

# Extract Intel MPI include path
if command -v mpicxx >/dev/null 2>&1; then
    MPI_INC="$(mpicxx -show | grep -o -- '-I[^ ]*' | head -n 1 | sed 's/^-I//' | tr -d '\"')"
else
    MPI_INC="/opt/intel/oneapi/mpi/latest/include"
fi

export MEMBRAIN_SITES_DIR="${BUILD_DIR}"

echo " -> Stage A: LLVM Pass Transformation on generator..."
cd "${BFS_DIR}/generator"
"${CLANG_CXX}" -x c -c -O3 -g -fopenmp \
    -fpass-plugin="${PASS_SO}" \
    -I"${INC_DIR}" \
    splittable_mrg.c -o splittable_mrg.o

echo " -> Stage A: LLVM Pass Transformation on mpi/main.cc..."
cd "${BUILD_DIR}"
"${CLANG_CXX}" -c -O3 -ffast-math -g -fopenmp \
    -include pthread.h -include time.h -include unistd.h -include stdexcept \
    -Drestrict=__restrict__ -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS \
    -DPAGE_SIZE=4096 \
    -DVERVOSE_MODE=0 -DVERTEX_REORDERING=2 -DEDGE_LIST_PREDISTRIBUTION -DNDEBUG -DPROFILE_REGIONS \
    -fpass-plugin="${PASS_SO}" \
    -I"${INC_DIR}" -I../external -I"${MPI_INC}" \
    main.cc -o main.o

echo " -> Merging Allocation Sites Metadata into allocation_sites.json..."
python3 "${MEMBRAIN_ROOT}/scripts/merge_allocation_sites.py" --dir "${BUILD_DIR}" --output "${BUILD_DIR}/allocation_sites.json"

echo " -> Stage B: Intel mpiicpx Native Linking..."
mpiicpx -O3 -ffast-math -xHost -g -fiopenmp main.o "${BFS_DIR}/generator/splittable_mrg.o" \
    -L"${RT_DIR}" -lmembrain_rt -Wl,-rpath,"${RT_DIR}" \
    -lnuma -lm \
    -o "${BUILD_DIR}/runnable"

if [ -f "${BUILD_DIR}/runnable" ]; then
    echo "[SUCCESS] MemBrain Graph500-BFS binary successfully built at:"
    echo "         ${BUILD_DIR}/runnable"
else
    echo "[ERROR] Build failed. Binary not found."
    exit 1
fi
