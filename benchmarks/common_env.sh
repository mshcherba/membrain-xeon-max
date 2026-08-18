#!/bin/bash
# ==============================================================================
# Common OpenMP and Hardware Affinity Environment Settings for MemBrain Benchmarks
# ==============================================================================

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"
export OMP_STACKSIZE="${OMP_STACKSIZE:-64M}"
export KMP_AFFINITY="${KMP_AFFINITY:-granularity=fine,compact,1,0}"
ulimit -s unlimited 2>/dev/null || true

