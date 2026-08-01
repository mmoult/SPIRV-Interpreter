# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
cmake_minimum_required(VERSION 3.28)
message(STATUS "Using CMake version: ${CMAKE_VERSION}")

project(SPIRV-Interpreter CXX)

# Set the version of C++ for the project
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "Compiling with: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_definitions(COMPILER_CLANG)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_definitions(COMPILER_GNU)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_definitions(COMPILER_MSVC)
endif()

# The set of warnings enabled for first-party code. Deliberately does NOT include -Werror: different compiler versions
# may have different warnings, and that can gate building.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set(SPV_WARNINGS
    -Wall
    -Wextra
    -Wnon-virtual-dtor
    -Wsuggest-override
    -Wnull-dereference
    -Wundef
    -Wimplicit-fallthrough
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND SPV_WARNINGS -Winconsistent-missing-override -Wconditional-uninitialized)
  endif()
elseif(MSVC)
  # /W4 on MSVC is dominated by conversion warnings that -Wall -Wextra does not report; silence those two so the
  # remaining output stays readable.
  set(SPV_WARNINGS /W4 /wd4244 /wd4267)
endif()

# Sanitizers are a whole-program property; a consumer that wants them should set CMAKE_CXX_FLAGS and
# CMAKE_EXE_LINKER_FLAGS, or use a toolchain file, which cover every target in the build rather than just ours.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(SPV_INTERP_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (Clang/GCC only)" OFF)
  if(SPV_INTERP_SANITIZE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
      add_link_options(-fsanitize=address,undefined)
      # Bounds-check container operator[] too. This caught an out-of-bounds read in Aggregate::equals, which
      # neither sanitizer sees because the access stays inside the heap block. Ignored when not on libstdc++.
      add_compile_definitions(_GLIBCXX_ASSERTIONS)
    else()
      message(WARNING "SPV_INTERP_SANITIZE is not supported for ${CMAKE_CXX_COMPILER_ID}; ignoring.")
    endif()
  endif()
endif()

# The library is defined unconditionally, so the project can be consumed with add_subdirectory(). Everything else,
# including the command line tool, the tests, and the install rules, exists only for a standalone build.
add_subdirectory(src)

# A consumer may have its own glm. SYSTEM marks the include directories as system headers, so third-party code does not
# report against our warning flags; without it, Catch2's headers alone account for dozens of -Wnon-virtual-dtor reports.
if(NOT TARGET glm::glm)
  add_subdirectory(external/glm SYSTEM)
endif()

# Assume most consumers don't want to build and run unit tests: default off
option(BUILD_TESTING "Build unit tests" OFF)
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  add_subdirectory(test)
  add_subdirectory(external/Catch2 SYSTEM)
endif()
