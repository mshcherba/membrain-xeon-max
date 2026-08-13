cmake_minimum_required(VERSION 3.28)

add_executable(test_umf_rt test_umf_rt.cpp)
target_link_libraries(test_umf_rt PRIVATE membrain_rt)
target_include_directories(test_umf_rt PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
