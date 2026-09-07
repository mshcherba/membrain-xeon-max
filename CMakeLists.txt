cmake_minimum_required(VERSION 3.28)
project(MemBrain CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add standard prefix search paths
list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/opt/intel/oneapi/umf/latest" "/opt/intel/oneapi/latest" "/opt/intel/oneapi/tbb/latest")
set(CMAKE_BUILD_RPATH "/opt/intel/oneapi/umf/latest/lib;/opt/intel/oneapi/tbb/latest/lib;${CMAKE_BINARY_DIR}/runtime")
set(CMAKE_INSTALL_RPATH "/opt/intel/oneapi/umf/latest/lib;/opt/intel/oneapi/tbb/latest/lib")

# Directories for MemBrain components
add_subdirectory(llvm-pass)
add_subdirectory(runtime)
add_subdirectory(tests)
