cmake_minimum_required(VERSION 3.28)
project(MemBrain CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add standard prefix search paths
list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/opt/intel/oneapi/umf/latest" "/opt/intel/oneapi/latest")

# Directories for MemBrain components
add_subdirectory(llvm-pass)
add_subdirectory(runtime)
add_subdirectory(tests)
