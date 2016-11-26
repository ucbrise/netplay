cmake_minimum_required(VERSION 2.8)
project(netplay-bench CXX)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

find_package(Threads REQUIRED)

set(INCLUDE include)
set(LOGSTORE_INCLUDE ../logstore/include)
set(PKTSTORE_INCLUDE ../netplayd/include)
include_directories(${INCLUDE} ${LOGSTORE_INCLUDE} ${PKTSTORE_INCLUDE})

add_executable(pktbench packet_bench.cc)

target_link_libraries(pktbench ${CMAKE_THREAD_LIBS_INIT})
