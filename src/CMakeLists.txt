# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)

add_library(spv-interp)

# Add other source files.
target_sources(spv-interp
  PRIVATE
    format/json.cpp
    format/parse.cpp
    format/yaml.cpp
    front/argparse.cpp
    front/console.cpp
    front/debug.cpp
    spv/data/data.cpp
    spv/data/manager.cpp
    spv/frame.cpp
    spv/inst-exec.cpp
    spv/inst-make.cpp
    spv/inst-read.cpp
    spv/instruction.cpp
    spv/program.cpp
    spv/ray-substage.cpp
    util/array-math.cpp
    util/compare.cpp
    util/fpconvert.cpp
    util/geom-math.cpp
    util/string.cpp
    util/trie.cpp
    util/stb-impl.cpp
    values/raytrace/accel-struct.cpp
    values/raytrace/node.cpp
    values/raytrace/trace.cpp
    values/aggregate.cpp
    values/image.cpp
    values/primitive.cpp
    values/statics.cpp
    values/type.cpp
)

target_link_libraries(spv-interp glm::glm)

# PRIVATE so the flags are not exported to consumers of spv-interp. Applied to every configuration rather than only
# Debug: a Release build is where an unnoticed warning does the most damage. SPV_WARNINGS never includes -Werror.
target_compile_options(spv-interp PRIVATE ${SPV_WARNINGS})

# stb is third-party; its warnings are not ours to fix and would drown out our own. Source-level COMPILE_OPTIONS are
# appended to the target's, so these have to be explicit negations rather than an empty list. Must stay above the guard
# below, since stb-impl.cpp belongs to spv-interp and so is built for consumers too.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_source_files_properties(util/stb-impl.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-missing-field-initializers;-Wno-implicit-fallthrough")
endif()

# Consumers include headers relative to src/, e.g. #include "spv/program.hpp". BUILD_INTERFACE only: there is no
# install or export set for the library yet, so this serves add_subdirectory consumption alone.
target_include_directories(spv-interp PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# Namespaced alias, so a consumer links the same name whether it vendored the project or (eventually) installed it.
add_library(SPIRV-Interpreter::spv-interp ALIAS spv-interp)

# Everything below builds the command line tool, which a consumer of the library does not want. The git and timestamp
# lookups are inside the guard too, so that consuming the project does not shell out to git or print version notes.
if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

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

add_executable(spv-run main.cpp)
set_target_properties(spv-run PROPERTIES OUTPUT_NAME "spirv-run")
target_compile_definitions(spv-run PRIVATE "HASH=${HASH}" "BUILD_DATE=${TODAY}")
target_link_libraries(spv-run spv-interp)
target_compile_options(spv-run PRIVATE ${SPV_WARNINGS})
install(TARGETS spv-run)
