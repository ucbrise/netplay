cmake_minimum_required(VERSION 2.8)
project(netplay-bench CXX)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

find_package(Threads REQUIRED)

set(INCLUDE include)
set(DPDK_INCLUDE ../dpdk/include)
set(LOGSTORE_INCLUDE ../logstore/include)
set(PKTSTORE_INCLUDE ../netplayd/include)
set(PKTGEN_INCLUDE ../pktgen/include)
include_directories(${INCLUDE} ${DPDK_INCLUDE} ${LOGSTORE_INCLUDE} 
  ${PKTSTORE_INCLUDE} ${PKTGEN_INCLUDE} ${DPDK_INCLUDE_DIR})

link_directories(${DPDK_LIB_DIR} ${THRIFT_LIB_DIR})

add_executable(pktbench packet_bench.cc)
add_executable(fbench filter_bench.cc)

set(DPDK_OPT -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive)
target_link_libraries(pktbench ${DPDK_OPT} ${CMAKE_THREAD_LIBS_INIT} dl)
target_link_libraries(fbench ${DPDK_OPT} ${CMAKE_THREAD_LIBS_INIT} dl)
