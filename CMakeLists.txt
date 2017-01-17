cmake_minimum_required(VERSION 2.8)
project(log-store CXX)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(COMPILER_SUPPORTS_CXX11)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
  message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -g3 -ggdb3 -Ofast -march=native -Werror -D_GNU_SOURCE -Wall -Wextra -Wcast-align -Wno-write-strings -Wno-missing-field-initializers")
enable_language(C)

OPTION(INDEX_SRC_IP "Enable indexing of source IPs" ON)
OPTION(INDEX_DST_IP "Enable indexing of destination IPs" ON)
OPTION(INDEX_SRC_PORT "Enable indexing of source ports" ON)
OPTION(INDEX_DST_PORT "Enable indexing of destination ports" ON)
OPTION(INDEX_TS "Enable indexing of timestamps" ON)

# Set 3rd party includes/libs
if(EXISTS ${PROJECT_SOURCE_DIR}/3rdparty/dpdk-16.07)
	set(DPDK_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/3rdparty/dpdk-16.07/build/include)
	set(DPDK_LIB_DIR ${PROJECT_SOURCE_DIR}/3rdparty/dpdk-16.07/build/lib)
else()
  message(FATAL_ERROR "Ensure DPDK is installed by runnning 3rdparty/setup.sh")
endif()

if(INDEX_SRC_IP)
  message(STATUS "Indexing enabled for source IPs")
  add_definitions(-DINDEX_SRC_IP=1)
else(INDEX_SRC_IP)
  message(STATUS "Indexing disabled for source IPs")
  add_definitions(-DINDEX_SRC_IP=0)
endif(INDEX_SRC_IP)

if(INDEX_DST_IP)
  message(STATUS "Indexing enabled for destination IPs")
  add_definitions(-DINDEX_DST_IP=1)
else(INDEX_DST_IP)
  message(STATUS "Indexing disabled for destination IPs")
  add_definitions(-DINDEX_DST_IP=0)
endif(INDEX_DST_IP)

if(INDEX_SRC_PORT)
  message(STATUS "Indexing enabled for source ports")
  add_definitions(-DINDEX_SRC_PORT=1)
else(INDEX_SRC_PORT)
  message(STATUS "Indexing disabled for source ports")
  add_definitions(-DINDEX_SRC_PORT=0)
endif(INDEX_SRC_PORT)

if(INDEX_DST_PORT)
  message(STATUS "Indexing enabled for destination ports")
  add_definitions(-DINDEX_DST_PORT=1)
else(INDEX_DST_PORT)
  message(STATUS "Indexing disabled for destination ports")
  add_definitions(-DINDEX_DST_PORT=0)
endif(INDEX_DST_PORT)

if(INDEX_TS)
  message(STATUS "Indexing enabled for timestamps")
  add_definitions(-DINDEX_TS=1)
else(INDEX_TS)
  message(STATUS "Indexing disabled for timestamps")
  add_definitions(-DINDEX_TS=0)
endif(INDEX_TS)

add_subdirectory(netplayd)
add_subdirectory(pktgen)
add_subdirectory(bench)

# Testing framework
# Build google testing framework
#add_subdirectory(3rdparty/gtest-1.7.0)

# Build and run all tests for succinct
#add_subdirectory(test)

#enable_testing()
#add_test(NPTest ${TESTS_PATH}/np_test)