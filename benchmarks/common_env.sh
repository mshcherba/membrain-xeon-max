#!/bin/bash
# ==============================================================================
# Common OpenMP and Hardware Affinity Environment Settings for MemBrain Benchmarks
# ==============================================================================

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"
export OMP_STACKSIZE="${OMP_STACKSIZE:-64M}"
export KMP_AFFINITY="${KMP_AFFINITY:-granularity=fine,compact,1,0}"
export MEMBRAIN_CLONE_DEPTH="${MEMBRAIN_CLONE_DEPTH:-4}"
export LD_LIBRARY_PATH="/opt/intel/oneapi/mkl/2026.1/lib:/opt/intel/oneapi/umf/1.1/lib:/opt/intel/oneapi/tbb/2023.1/lib:/opt/intel/oneapi/compiler/2026.1/lib:/opt/intel/oneapi/mpi/2021.18/lib:/users/maksym/membrain-xeon-max/build/runtime:${LD_LIBRARY_PATH}"
ulimit -s unlimited 2>/dev/null || true

