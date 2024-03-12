# SPIRV-Interpreter

An interpreter for SPIR-V shaders/kernels. Outputs the result for given inputs.

## Features
- Support for fragment shaders, ...
- Specify inputs in TOML, with other formats coming soon
- Can generate template file for expected inputs
- Option to check against expected results
- Verbose trace of program execution (coming soon)
- 2 test examples, and counting

## Use
SPIRV-Interpreter can be built with CMake. Its source follows the C++20 standard.

...

## Contributing
Contributions via merge requests are welcome! Contributions should:
- Be provided under the same license as the project (MPL 2.0)
- Follow the coding style. You may use clang-format to auto-format.
- Be well-documented and have test cases (in the examples file)
- Not break other test cases. Include the results of `test/example-runner.py` in your request

### License
Full license terms are in [LICENSE.md](LICENSE.md).

```
© SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
```
