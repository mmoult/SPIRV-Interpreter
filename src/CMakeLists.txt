cmake_minimum_required(VERSION 3.28)

project(cxx_modules_hello CXX)

# Set the version of C++ for the project
set(CMAKE_CXX_STANDARD 20)

add_executable(build)
set_target_properties(build PROPERTIES OUTPUT_NAME "hello")

# Add module files
target_sources(build
  PUBLIC
    FILE_SET CXX_MODULES FILES
      foo.cxx
)
# Add regular files (if any)
target_sources(build
  PUBLIC
    main.cpp
)
