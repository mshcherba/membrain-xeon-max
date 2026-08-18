#!/bin/bash
# ==============================================================================
# System Dependencies and Intel oneAPI Installer Script.
#
# Description:
#   Installs required build utilities (cmake, build-essential, g++-14, git)
#   and sets up the Intel APT repository to install the Intel oneAPI DPC++/C++
#   compiler (intel-oneapi-compiler-dpcpp-cpp).
#
# Usage:
#   sudo ./scripts/install_dependencies.sh
#
# Requirements:
#   Must be run with root privileges (sudo).
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] Installation requires root privileges."
    echo "Usage: sudo $0"
    exit 1
fi

echo "[INFO] Installing system build dependencies (LLVM, Clang, NUMA, CMake)..."

apt-get update
apt-get install -y wget gpg cmake build-essential g++-14 git clang llvm-dev libomp-dev libnuma-dev numactl python3 python3-pip python3-scipy python3-numpy

# Setup Intel oneAPI APT repository if missing
if [ ! -f /usr/share/keyrings/oneapi-archive-keyring.gpg ]; then
    wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
fi

if [ ! -f /etc/apt/sources.list.d/oneAPI.list ]; then
    echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | tee /etc/apt/sources.list.d/oneAPI.list
    apt-get update
fi

echo "[INFO] Installing Intel oneAPI compiler and MPI development packages..."
apt-get install -y intel-oneapi-compiler-dpcpp-cpp intel-oneapi-mpi-devel

echo "[SUCCESS] Dependencies installed successfully."
