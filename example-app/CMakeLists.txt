cmake_minimum_required(VERSION 3.20)
project(clangsolv_example LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CLANGSOLV_CHECKER "" CACHE FILEPATH "Path to the built clangsolv executable")
if(NOT EXISTS "${CLANGSOLV_CHECKER}")
  message(FATAL_ERROR "Build the checker from the repository root and set -DCLANGSOLV_CHECKER to its executable")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ClangSOLV.cmake")
add_executable(clangsolv_app src/main.cpp src/workflow.cpp src/smart_pointers.cpp)
target_include_directories(clangsolv_app PRIVATE include)
clangsolv_enable(clangsolv_app clangsolv.properties CHECKER "${CLANGSOLV_CHECKER}"
  SOURCES src/main.cpp src/workflow.cpp src/smart_pointers.cpp
  OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
  DEPENDS include/workflow.hpp)
enable_testing()
add_test(NAME workflow-example COMMAND clangsolv_app)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME rejected-examples COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_examples.py"
  --checker "${CLANGSOLV_CHECKER}" --generated "${CMAKE_CURRENT_BINARY_DIR}/clangsolv-generated/clangsolv_app")
