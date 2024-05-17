# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)

add_library(spv-interp)

# Add module interface files
target_sources(spv-interp
  PUBLIC
    FILE_SET CXX_MODULES FILES
      format/json.cxx
      format/parse.cxx
      format/yaml.cxx
      spv/data.cxx
      spv/frame.cxx
      spv/instruction.cxx
      spv/program.cxx
      spv/token.cxx
      util/compare.cxx
      values/aggregate.cxx
      values/pointer.cxx
      values/primitive.cxx
)
# Add other source files
target_sources(spv-interp
  PUBLIC
    spv/inst-exec.cpp
    spv/inst-make.cpp
    spv/inst-read.cpp
    values/type.cpp
)

add_executable(spv-run main.cpp)
set_target_properties(spv-run PROPERTIES OUTPUT_NAME "spirv-run")
target_link_libraries(spv-run spv-interp)
install(TARGETS spv-run)
