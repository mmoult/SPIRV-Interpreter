# SPIRV-Interpreter

An interpreter for SPIR-V shaders/kernels, which aims to support the full
[SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html). Outputs results of running the
program for given inputs.

## Features
- Support for vertex, fragment, compute, hull, and raytracing shaders
- Specify inputs and print outputs in YAML or JSON
- Textures may be read from and written to .png files
- Generate template files for expected inputs
- Check against expected results
- Verbose trace and interactive program execution
- 75 test examples, and counting

## Limitations

### Validation
For the sake of runtime speed and code clarity, the interpreter expects syntactically correct SPIR-V inputs. It is not,
nor does it intend to be, a SPIR-V validator. If that is what you need, use `spirv-val` in
[SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools). If the input SPIR-V file is invalid, the behavior of the
interpretation is undefined. Correctness checking is not a high priority since most SPIR-V is auto-generated.

### Image Processing
The SPIR-V specification does not explicitly define how some image operations should be done. This presents some
challenges to the interpreter which aims to be a correct reference.

Many vendors use anisotropic filtering, but these algorithms have not been open sourced. Therefore, the interpreter uses
n-linear interpolation instead, which should be easy to deconstruct and/or extend as needed.

### Ray Tracing
Similarly to image processing, the SPIR-V specification does not define all characteristics of ray tracing functions and
structures. A best guess approximation is used for the interpreter, which relies on information common to all
implementations.

## Building
The source can be built using a compiler which supports C++20. It is known to build on Linux and Windows.

Here are a few build commands to run from the repository's root directory:

```
git submodule update --init --recursive
cmake -B build -G Ninja
ninja -C build all
```

assuming you have [cmake](https://github.com/Kitware/CMake) version ≥ 3.28 and
[ninja](https://github.com/ninja-build/ninja) version ≥ 1.11 installed.

After building, you should find the `spirv-run` executable at `build/src/spirv-run`.

## Use
Use `-h` or `--help` on the `spirv-run` executable to see the definitive list of command line arguments and flags. Some
options (especially the more complex ones) have more information described in [Usage Help](usage-help.md).

## Testing
The project has two complementary approaches to testing:

1) Integration tests on SPIR-V shaders in `examples`, commonly launched through `test/example-runner.py`.
2) Unit tests via [Catch2](https://github.com/catchorg/Catch2) in the `test` directory

> [!NOTE]
> You *do not* need to have Catch2 to build or run the `spirv-run` executable or the integration tests.

If you wish to run the unit tests, follow the steps to build, also including `-DBUILD_TESTING=ON` as an argument to
`cmake`. After building, you should find a `tests` executable at `build/test/tests`.

## Contributing
Contributions via merge requests are welcome! Contributions should:
- Be provided under the same license as the project (MPL 2.0).
- Follow the coding style. We recommend using `clang-format` (see settings at [.clang-format](src/.clang-format)), which
  can be done easily with
  [git-clang-format](https://github.com/llvm/llvm-project/blob/main/clang/tools/clang-format/git-clang-format).
- Be well-documented and have test cases (in `examples` and/or `test` directories).
- Not break other code. Run `test/example-runner.py` and `build/test/tests` as proof.

## License
The interpreter's novel source is licensed with the MPL v2.0. Full license terms are in [LICENSE.md](LICENSE.md).

```
© SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
```

There are a few code dependencies. You may review each individually (the last two are handled via git's submodule, so
the links may not work until they are initialized) for the complete terms:

- [SPIR-V](external/spirv.hpp): Custom Permissive License
- [GLSL](external/GLSL.std.450.h): Custom Permissive License
- [stb](external/stb): dual-licensed public domain or MIT
- [glm](external/glm): MIT License
- [Catch2](external/Catch2): Boost Software License

Examples in [examples](examples) may use their own licenses. Each has a `LICENSE` file with the necessary documentation.
Examples are only for testing and demonstrating behavior- they are not bundled in the `spirv-run` executable or any
other project build (and as such, the interpreter is *not* a "derivative work" in the legal sense).
