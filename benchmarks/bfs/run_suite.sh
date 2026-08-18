#!/bin/bash
# ==============================================================================
# Graph500-BFS Benchmark Tiering Suite Runner Shortcut.
#
# Usage:
#   ./run_suite.sh [--repeats N] [--capacities 12.5 25.0 50.0] [BFS_ARGS...]
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEMBRAIN_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUNNER="${MEMBRAIN_ROOT}/scripts/run_benchmark_suite.py"

if [ -f /opt/intel/oneapi/setvars.sh ]; then
    source /opt/intel/oneapi/setvars.sh --force > /dev/null 2>&1 || true
fi

exec python3 "${RUNNER}" --benchmark "bfs" "$@"
