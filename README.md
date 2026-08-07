# MemBrain: Automated Application Guidance for Hybrid Memory Systems

MemBrain is an automated data management framework for hybrid memory architectures (Intel Xeon Max HBM2e + DDR5).

## Structure
- `llvm-pass/`: LLVM Compiler Pass plugin for `icpx` / `clang++` (NewPassManager)
- `runtime/`: MemBrain Runtime Allocator (interposition layer for UMF/memkind)
- `profiler/`: PEBS-based memory profiler
- `optimizer/`: Thermos bin-packing algorithm
- `scripts/`: Build and evaluation scripts
