# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)

add_subdirectory(Catch2)

# These tests can use the Catch2-provided main
add_executable(tests)
set_target_properties(tests PROPERTIES OUTPUT_NAME "tests")

target_sources(tests
    PUBLIC
      util-test.cpp
)

target_link_libraries(tests PRIVATE Catch2::Catch2WithMain spv-interp)
