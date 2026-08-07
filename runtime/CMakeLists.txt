cmake_minimum_required(VERSION 3.14)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(UMF umf)
endif()


add_library(membrain_rt SHARED membrain_rt.cpp)

if(UMF_FOUND)
    target_include_directories(membrain_rt PRIVATE ${UMF_INCLUDE_DIRS})
    target_link_libraries(membrain_rt PRIVATE ${UMF_LIBRARIES})
else()
    message(STATUS "UMF pkg-config not found, using direct fallback linkage")
    target_link_libraries(membrain_rt PRIVATE numa dl)
endif()
