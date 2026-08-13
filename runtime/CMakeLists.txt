cmake_minimum_required(VERSION 3.28)

find_package(umf REQUIRED CONFIG)

add_library(membrain_rt SHARED membrain_rt.cpp topology_config.cpp guidance_parser.cpp pagemap_util.cpp)
target_include_directories(membrain_rt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(membrain_rt PRIVATE umf::umf numa dl hwloc)
target_compile_definitions(membrain_rt PRIVATE HAVE_UMF=1)
