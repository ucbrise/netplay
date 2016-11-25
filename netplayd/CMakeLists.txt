cmake_minimum_required(VERSION 2.8)
project(netplayd CXX)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

find_package(Threads REQUIRED)

set(INCLUDE include)
set(DPDK_INCLUDE ../dpdk/include)
set(LOGSTORE_INCLUDE ../logstore/include)
set(NETPLAY_THRIFT_INCLUDE ../thrift/include)

include_directories(${INCLUDE} ${DPDK_INCLUDE} ${LOGSTORE_INCLUDE} 
  ${NETPLAY_THRIFT_INCLUDE} ${DPDK_INCLUDE_DIR} ${THRIFT_INCLUDE_DIR})

link_directories(${DPDK_LIB_DIR} ${THRIFT_LIB_DIR})

add_executable(netplayd netplayd.cc)
set(DPDK_OPT -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive)
target_link_libraries(netplayd npthrift ${CMAKE_THREAD_LIBS_INIT} ${DPDK_OPT}  dl)
