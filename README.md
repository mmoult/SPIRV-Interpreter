# SPIRV-Interpreter

An interpreter for SPIR-V shaders/kernels. Outputs the result after running the program on the given inputs.

## Features
- Support for vertex, fragment, ... shaders
- Specify inputs in TOML, with other formats coming soon
- Can generate template file for expected inputs
- Option to check against expected results
- Verbose trace of program execution (coming soon)
- 3 test examples, and counting

## Use
The source of SPIRV-Interpreter follows the C++20 standard and uses modules. It can be built with CMake version 3.28 or
higher. For example, you can do:

```
cmake -B build -S . -G Ninja
ninja -C build all
```

from the root directory, assuming you have [cmake](https://github.com/Kitware/CMake) and
[ninja](https://github.com/ninja-build/ninja) installed.

After building, you should see the `spirv-run` executable at `build/src/spirv-run`.  Use `-h` or `--help` on the
executable to see command line arguments and flags.

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

then follow the steps to build, but include `-DBUILD_TESTING=ON` as an argument to cmake. After building, you should
find a `tests` executable at `build/test/tests`.

## Contributing
Contributions via merge requests are welcome! Contributions should:
- Be provided under the same license as the project (MPL 2.0).
- Follow the coding style. We recommend using clang-format (see [.clang-format](src/.clang-format)).
- Be well-documented and have test cases (in `examples` and/or `test` directories).
- Not break other test cases. Include the results of `test/example-runner.py` in your request.

### License
Full license terms are in [LICENSE.md](LICENSE.md).

```
© SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
```
