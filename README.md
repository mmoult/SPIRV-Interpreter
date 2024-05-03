# SPIRV-Interpreter

An interpreter for SPIR-V shaders/kernels. Outputs the result after running the program on the given inputs.

## Features
- Support for vertex, fragment, compute, ... shaders
- Specify inputs and print outputs in YAML or JSON
- Can generate template file for expected inputs
- Option to check against expected results
- Verbose trace of program execution
- 5 test examples, and counting

## Use
This project aims to support the most recent versions of the full
[SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html). It generally expects
syntactically-correct SPIR-V files. It is not, nor does it intend to be, a SPIR-V validator. If that is what you need,
use `spirv-val` in [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools). If the input SPIR-V file is syntactically
incorrect, the behavior of interpretation is undefined. This should not be a problem since most SPIR-V is
auto-generated.

Static recursion is not *technically* forbidden by the SPIR-V spec, but it is forbidden by Vulkan (see
`VUID-StandaloneSpirv-None-04634`). For the sake of simplicity and efficiency, the interpreter does not guarantee
correct execution of programs with recursion.

Use `-h` or `--help` on the `spirv-run` executable to see command line arguments and flags.

## Building
The source can be built with CMake version 3.28 or higher using a compiler which supports C++20 with modules (such as
clang++ version ≥ 16 or g++ version ≥ 14). The source should be platform independent, but little to no testing has been
done on Windows or Mac.

Here are a couple example commands to run from the repository's root directory:

```
cmake -B build -S . -G Ninja
ninja -C build all
```

assuming you have [cmake](https://github.com/Kitware/CMake) version ≥ 3.28 and
[ninja](https://github.com/ninja-build/ninja) version ≥ 1.11 installed.

After building, you should find the `spirv-run` executable at `build/src/spirv-run`.

## Testing
The project has two complementary approaches to testing:

1) Integration tests on SPIR-V shaders in `examples`, commonly launched through `tests/example-runner.py`.
2) Unit tests via [Catch2](https://github.com/catchorg/Catch2) in the `test` directory

> [!NOTE]
> You *do not* need to have Catch2 to build or run the `spirv-run` executable or the integration tests.

If you wish to run the unit tests, first verify that the submodule has been initialized:

```
git submodule update ---init --recursive
```

then follow the steps to build, also including `-DBUILD_TESTING=ON` as an argument to cmake. After building, you should
find a `tests` executable at `build/test/tests`.

## Contributing
Contributions via merge requests are welcome! Contributions should:
- Be provided under the same license as the project (MPL 2.0).
- Follow the coding style. We recommend using clang-format (see [.clang-format](src/.clang-format)).
- Be well-documented and have test cases (in `examples` and/or `test` directories).
- Not break other code. Include the results of running `test/example-runner.py` and `tests` in requests as proof.

### License
Full license terms are in [LICENSE.md](LICENSE.md).

```
© SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
```
