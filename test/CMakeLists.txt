cmake_minimum_required(VERSION 3.14)

# project name
project(
  async_tests
  VERSION 1.0
  LANGUAGES CXX
)

# create test executable
file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)
add_executable(${PROJECT_NAME} ${sources})

# link the headers
target_link_libraries(${PROJECT_NAME} PRIVATE async)

# compiler options
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_compile_options(${PROJECT_NAME} PRIVATE -g -Wall -Wextra -pedantic)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
