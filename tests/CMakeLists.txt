cmake_minimum_required(VERSION 3.28)

add_executable(test_umf_rt test_umf_rt.cpp)
target_link_libraries(test_umf_rt PRIVATE membrain_rt)
target_include_directories(test_umf_rt PRIVATE ${CMAKE_SOURCE_DIR}/runtime)

add_executable(test_pagemap test_pagemap.cpp)
target_link_libraries(test_pagemap PRIVATE membrain_rt)
target_include_directories(test_pagemap PRIVATE ${CMAKE_SOURCE_DIR}/runtime)

add_executable(test_site_pool_manager test_site_pool_manager.cpp)
target_link_libraries(test_site_pool_manager PRIVATE membrain_rt)
target_include_directories(test_site_pool_manager PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
