cmake_minimum_required(VERSION 2.8)
project(netplay-thrift CXX)

find_package(Thrift REQUIRED)

set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
file(MAKE_DIRECTORY ${LIBRARY_OUTPUT_PATH})

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(LOGSTORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../logstore/include)

file(GLOB_RECURSE THRIFT_SOURCES *.cc)

include_directories(${INCLUDE_DIR} ${LOGSTORE_INCLUDE_DIR})

add_library(npthrift ${THRIFT_SOURCES})

target_link_libraries(npthrift ${THRIFT_LIBRARIES})