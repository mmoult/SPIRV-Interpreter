# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)

project(SPIRV-Interpreter CXX)

# Set the version of C++ for the project
set(CMAKE_CXX_STANDARD 20)

add_executable(build)
set_target_properties(build PROPERTIES OUTPUT_NAME "spirv-run")

# Add module interface files
target_sources(build
  PUBLIC
    FILE_SET CXX_MODULES FILES
      format/toml.cxx
      spv/data.cxx
      spv/frame.cxx
      spv/instruction.cxx
      spv/program.cxx
      spv/token.cxx
      values/aggregate.cxx
      values/pointer.cxx
      values/primitive.cxx
)
# Add other source files
target_sources(build
  PUBLIC
    main.cpp
    values/type.cpp
)
