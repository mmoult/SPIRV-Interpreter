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
      front/argparse.cxx
      front/console.cxx
      front/debug.cxx
      spv/data/data.cxx
      spv/frame.cxx
      spv/instruction.cxx
      spv/program.cxx
      spv/ray-flags.cxx
      spv/token.cxx
      util/compare.cxx
      util/intersection.cxx
      util/string.cxx
      values/aggregate.cxx
      values/image.cxx
      values/pointer.cxx
      values/primitive.cxx
      values/statics.cxx
      values/string.cxx
      values/raytrace/accel-struct.cxx
      #values/raytrace/ray-query.cxx
      values/raytrace/shader-binding-table.cxx
)
# Add other source files
target_sources(spv-interp
  PUBLIC
    spv/data/manager.cpp
    spv/inst-exec.cpp
    spv/inst-make.cpp
    spv/inst-read.cpp
    util/trie.cpp
    values/type.cpp
    #values/raytrace/shader-binding-table.cpp
    values/raytrace/node.cpp
    values/raytrace/trace.cpp
)

# git rev-parse --short HEAD

set(HASH "" CACHE STRING "Repository hash to insert into version info")
set(GIT_EXECUTABLE "git" CACHE STRING "The path to a git executable, or just git if on path")

# If the hash is set, skip the generation by using git
if(HASH STREQUAL "")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    OUTPUT_VARIABLE HASH
    RESULT_VARIABLE git_success
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(git_success)
    message(WARNING "Could not automatically determine version hash. Please set HASH or GIT_EXECUTABLE.")
  endif()
endif()
message(STATUS "found hash: ${HASH}")

add_subdirectory(external/glm)
target_link_libraries(spv-interp glm::glm)

add_executable(spv-run main.cpp)
set_target_properties(spv-run PROPERTIES OUTPUT_NAME "spirv-run")
target_compile_options(spv-run PUBLIC "-DHASH=${HASH}")
target_compile_options(spv-run PUBLIC "$<$<CONFIG:DEBUG>:-Wall>")
target_link_libraries(spv-run spv-interp)
install(TARGETS spv-run)
