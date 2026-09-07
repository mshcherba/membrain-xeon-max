cmake_minimum_required(VERSION 3.28)

find_package(umf REQUIRED CONFIG)
find_package(TBB REQUIRED COMPONENTS tbbmalloc CONFIG PATHS /opt/intel/oneapi/tbb/latest/lib/cmake/tbb)

add_library(membrain_rt SHARED
    membrain_rt.cpp
    topology_config.cpp
    guidance_parser.cpp
    pagemap_util.cpp
    site_pool_manager.cpp
    pebs_sampler.cpp
)

target_include_directories(membrain_rt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    /opt/intel/oneapi/umf/latest/include
    /opt/intel/oneapi/umf/1.1/include
    /opt/intel/oneapi/tbb/latest/include
)

target_compile_options(membrain_rt PUBLIC
    -I/opt/intel/oneapi/umf/latest/include
    -I/opt/intel/oneapi/tbb/latest/include
)

target_link_libraries(membrain_rt PUBLIC
    umf::umf
    -Wl,--no-as-needed
    TBB::tbbmalloc
    -Wl,--as-needed
    numa
    dl
    hwloc
)

target_compile_definitions(membrain_rt PRIVATE HAVE_UMF=1)

set_target_properties(membrain_rt PROPERTIES
    BUILD_RPATH "/opt/intel/oneapi/umf/latest/lib;/opt/intel/oneapi/tbb/latest/lib"
    INSTALL_RPATH "/opt/intel/oneapi/umf/latest/lib;/opt/intel/oneapi/tbb/latest/lib"
)
