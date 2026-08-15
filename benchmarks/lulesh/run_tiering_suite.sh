#!/bin/bash
# MemBrain Memory Tiering Experiment Suite Runner
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || true
fi

cd "${MEMBRAIN_ROOT}"

echo "[INFO] Generating all guidance files..."
python3 "${SCRIPT_DIR}/generate_guidance.py"

echo "[INFO] Launching MemBrain Tiering Benchmark Suite..."
python3 "${SCRIPT_DIR}/run_tiering_suite.py"
