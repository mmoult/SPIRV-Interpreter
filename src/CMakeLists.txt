# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)

add_library(spv-interp)

# Add other source files
target_sources(spv-interp
  PUBLIC
    format/json.cpp
    format/parse.cpp
    format/yaml.cpp
    front/argparse.cpp
    front/console.cpp
    front/debug.cpp
    spv/data/manager.cpp
    spv/inst-exec.cpp
    spv/inst-make.cpp
    spv/inst-read.cpp
    util/trie.cpp
    util/stb-impl.cpp
    values/type.cpp
    values/raytrace/node.cpp
    values/raytrace/trace.cpp
)

set(HASH "" CACHE STRING "Repository hash to insert into version info")
set(GIT_EXECUTABLE "git" CACHE STRING "The path to a git executable, or just git if on path")

# Detect the git hash unless already provided
if(HASH STREQUAL "")
  # git rev-parse --short HEAD
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    OUTPUT_VARIABLE HASH
    RESULT_VARIABLE git_fail
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(git_fail)
    message(WARNING "Could not automatically determine version hash. Please set HASH or GIT_EXECUTABLE.")
  endif()
endif()
message(STATUS "Latest git commit hash: ${HASH}")

string(TIMESTAMP TODAY "%Y-%m-%d")
message(STATUS "Current build date: ${TODAY}")

target_link_libraries(spv-interp glm::glm)

add_executable(spv-run main.cpp)
set_target_properties(spv-run PROPERTIES OUTPUT_NAME "spirv-run")
target_compile_options(spv-run PUBLIC "-DHASH=${HASH}" "-DBUILD_DATE=${TODAY}")
target_compile_options(spv-run PUBLIC "$<$<CONFIG:DEBUG>:-Wall>")
target_link_libraries(spv-run spv-interp)
install(TARGETS spv-run)
