cmake_minimum_required(VERSION 3.14)

# project name
project(
  async_tests
  VERSION 1.0
  LANGUAGES CXX
)

# create async executable
file(GLOB async_test CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/async_tests.cc)
add_executable(async_tests ${async_test})

# link the headers
target_link_libraries(async_tests PRIVATE async)

# compiler options
set_target_properties(async_tests PROPERTIES CXX_STANDARD 17)
target_compile_options(async_tests PRIVATE -g -Wall -Wextra -pedantic)
target_compile_features(async_tests PRIVATE cxx_std_17)

# create cyan executable
file(GLOB cyan_test CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cyan_tests.cc)
add_executable(cyan_tests ${cyan_test})

# link the headers
target_link_libraries(cyan_tests PRIVATE cyan)

# compiler options
set_target_properties(cyan_tests PROPERTIES CXX_STANDARD 17)
target_compile_options(cyan_tests PRIVATE -g -Wall -Wextra -pedantic)
target_compile_features(cyan_tests PRIVATE cxx_std_17)
