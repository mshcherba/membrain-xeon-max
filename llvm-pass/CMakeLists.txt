cmake_minimum_required(VERSION 3.14)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Dynamic LLVM Pass plugin
add_library(MemBrainPass MODULE MemBrainPass.cpp)

set_target_properties(MemBrainPass PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
