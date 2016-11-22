cmake_minimum_required(VERSION 2.8)
project(netplay-test CXX)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})
set(TESTS_PATH ${EXECUTABLE_OUTPUT_PATH} PARENT_SCOPE)

find_package(dpdk REQUIRED)

set(INCLUDE ../dpdk/include)

include_directories(${gtest_SOURCE_DIR}/include ${INCLUDE} ${DPDK_INCLUDE_DIR})

file(GLOB_RECURSE test_sources src/*.cc)
add_executable(np_test ${test_sources})
target_link_libraries(np_test gtest_main ${DPDK_LIBRARIES} dl)
