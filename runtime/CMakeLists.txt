cmake_minimum_required(VERSION 3.28)

find_package(umf REQUIRED CONFIG)

add_library(membrain_rt SHARED membrain_rt.cpp topology_config.cpp guidance_parser.cpp pagemap_util.cpp site_pool_manager.cpp pebs_sampler.cpp)
target_include_directories(membrain_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} /opt/intel/oneapi/umf/latest/include /opt/intel/oneapi/umf/1.1/include)
target_link_libraries(membrain_rt PUBLIC umf::umf numa dl hwloc)
target_compile_definitions(membrain_rt PRIVATE HAVE_UMF=1)
