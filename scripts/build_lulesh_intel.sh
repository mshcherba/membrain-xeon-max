#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LULESH_DIR="${LULESH_DIR:-$(cd "${MEMBRAIN_ROOT}/.." && pwd)/LULESH}"

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

# Source Intel oneAPI environment if setvars.sh exists
if [ -f /opt/intel/oneapi/setvars.sh ]; then
    echo "[INFO] Sourcing Intel oneAPI environment..."
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || source /opt/intel/oneapi/setvars.sh
fi

if ! command -v icpx >/dev/null 2>&1; then
    echo "[ERROR] Intel C++ compiler (icpx) is not available in PATH."
    exit 1
fi

# Clone LULESH if directory does not exist
if [ ! -d "${LULESH_DIR}" ]; then
    echo "[INFO] LULESH repository not found at ${LULESH_DIR}. Cloning from GitHub..."
    git clone https://github.com/LLNL/LULESH.git "${LULESH_DIR}"
fi

BUILD_INTEL_DIR="${LULESH_DIR}/build_intel"
echo "[INFO] Building standard LULESH with Intel oneAPI in: ${BUILD_INTEL_DIR}"

rm -rf "${BUILD_INTEL_DIR}"
mkdir -p "${BUILD_INTEL_DIR}"
cd "${BUILD_INTEL_DIR}"

CC=icx CXX=icpx cmake "${LULESH_DIR}" \
    -DCMAKE_CXX_FLAGS="-O3 -ffast-math -xHost" \
    -DWITH_MPI="${WITH_MPI:-Off}" \
    -DWITH_OPENMP=On

cmake --build . -- -j$(nproc)

if [ -f "${BUILD_INTEL_DIR}/lulesh2.0" ]; then
    echo "[SUCCESS] LULESH binary successfully built at:"
    echo "         ${BUILD_INTEL_DIR}/lulesh2.0"
else
    echo "[ERROR] Build failed. Binary not found."
    exit 1
fi
