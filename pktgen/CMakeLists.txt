cmake_minimum_required(VERSION 2.8)
project(pktgen CXX)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

find_package(dpdk REQUIRED)
find_package(Threads REQUIRED)

set(INCLUDE include)
set(DPDK_INCLUDE ../dpdk/include)

include_directories(${INCLUDE} ${DPDK_INCLUDE} ${DPDK_INCLUDE_DIR})

add_executable(pktgen pktgen.cc)
set(DPDK_OPT -Wl,--whole-archive ${DPDK_LIBRARY} -Wl,--no-whole-archive)
target_link_libraries(pktgen ${DPDK_OPT} dl ${CMAKE_THREAD_LIBS_INIT})
