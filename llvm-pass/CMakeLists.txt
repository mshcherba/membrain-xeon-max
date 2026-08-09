cmake_minimum_required(VERSION 3.28)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Dynamic LLVM Pass plugin
add_library(MemBrainPass MODULE MemBrainPass.cpp)

target_include_directories(MemBrainPass PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(MemBrainPass PRIVATE ${LLVM_DEFINITIONS})

set_target_properties(MemBrainPass PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

