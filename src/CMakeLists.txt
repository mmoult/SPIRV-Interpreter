cmake_minimum_required(VERSION 3.28)

project(SPIRV-Interpreter CXX)

# Set the version of C++ for the project
set(CMAKE_CXX_STANDARD 20)

add_executable(build)
set_target_properties(build PROPERTIES OUTPUT_NAME "spirv-run")

# Add module files
target_sources(build
  PUBLIC
    FILE_SET CXX_MODULES FILES
      toml.cxx
      utils.cxx
      value.cxx
      spv/data.cxx
      spv/frame.cxx
      spv/instructions.cxx
      spv/program.cxx
      spv/tokens.cxx
)
# Add regular files (if any)
target_sources(build
  PUBLIC
    main.cpp
)
