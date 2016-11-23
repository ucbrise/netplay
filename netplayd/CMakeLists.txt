cmake_minimum_required(VERSION 2.8)
project(netplayd CXX)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

find_package(dpdk REQUIRED)
find_package(Threads REQUIRED)

set(INCLUDE include)
set(DPDK_INCLUDE ../dpdk/include)
set(LOGSTORE_INCLUDE ../logstore/include)
set(PKTGEN_INCLUDE ../pktgen/include)

include_directories(${INCLUDE} ${DPDK_INCLUDE} ${LOGSTORE_INCLUDE} 
  ${PKTGEN_INCLUDE} ${DPDK_INCLUDE_DIR})

add_executable(netplayd netplayd.cc)
target_link_libraries(netplayd ${CMAKE_THREAD_LIBS_INIT} ${DPDK_LIBRARIES} dl)
