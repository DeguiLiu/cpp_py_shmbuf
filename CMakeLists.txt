cmake_minimum_required(VERSION 3.10)
project(cpp_py_shmbuf VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library
add_library(shm_channel INTERFACE)
target_include_directories(shm_channel INTERFACE include)

# Platform-specific link libraries
if(UNIX AND NOT APPLE)
  target_link_libraries(shm_channel INTERFACE rt pthread)
elseif(APPLE)
  # macOS: no -lrt needed
endif()

# Tests
option(SHM_BUILD_TESTS "Build tests" ON)
if(SHM_BUILD_TESTS)
  enable_testing()

  add_executable(test_byte_ring_buffer tests/test_byte_ring_buffer.cpp)
  target_link_libraries(test_byte_ring_buffer PRIVATE shm_channel)
  add_test(NAME test_byte_ring_buffer COMMAND test_byte_ring_buffer)

  add_executable(test_cross_lang_producer tests/test_cross_lang_producer.cpp)
  target_link_libraries(test_cross_lang_producer PRIVATE shm_channel)
endif()

# Examples (Linux/macOS only, uses usleep)
option(SHM_BUILD_EXAMPLES "Build examples" OFF)
if(SHM_BUILD_EXAMPLES AND NOT WIN32)
  add_executable(cpp_producer examples/cpp_producer.cpp)
  target_link_libraries(cpp_producer PRIVATE shm_channel)
endif()
