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

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake-modules)

add_subdirectory(netplayd)
add_subdirectory(pktgen)

# Testing framework
# Build google testing framework
#add_subdirectory(external/gtest-1.7.0)

# Build and run all tests for succinct
#add_subdirectory(test)

#enable_testing()
#add_test(NPTest ${TESTS_PATH}/np_test)
