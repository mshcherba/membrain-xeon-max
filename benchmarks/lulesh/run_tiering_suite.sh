#!/bin/bash
# MemBrain Memory Tiering Experiment Suite Runner
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh > /dev/null 2>&1 || true
fi

cd "${ROOT_DIR}"

echo "[INFO] Generating all guidance files..."
python3 scripts/generate_all_guidance.py

echo "[INFO] Launching MemBrain Tiering Benchmark Suite..."
python3 scripts/run_tiering_suite.py
