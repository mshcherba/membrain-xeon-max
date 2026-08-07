cmake_minimum_required(VERSION 3.14)
project(MemBrain CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Directories for MemBrain components
add_subdirectory(llvm-pass)
add_subdirectory(runtime)
