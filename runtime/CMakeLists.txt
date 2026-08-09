cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED umf_DIR AND EXISTS "/opt/intel/oneapi/umf/latest/lib/cmake/umf")
    set(umf_DIR "/opt/intel/oneapi/umf/latest/lib/cmake/umf")
endif()

find_package(umf REQUIRED)

add_library(membrain_rt SHARED membrain_rt.cpp)
target_link_libraries(membrain_rt PRIVATE umf::umf numa dl)
target_compile_definitions(membrain_rt PRIVATE HAVE_UMF=1)



