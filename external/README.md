# External Sources

There are two dependency header files authored by Khronos:

- GLSL.std.450.h
- spirv.hpp

These files should be periodically updated from https://registry.khronos.org/SPIR-V/, which itself gets source files
from https://github.com/KhronosGroup/SPIRV-Headers/tree/main/include/spirv/unified1

Small edits may need to be made to these external files. For each file changed, you should find a a change file with the
same name but with a `-CHANGES` suffix. These change files have been created by doing:

`diff -u <original> <updated> > <original>-CHANGES`

where `<original>` is the name of the original file and `<updated>` is the name of the file with edits.

After updating a file, apply edits by running:

`patch <file-name> < <file-name>-CHANGES`

where `<file-name>` is the name of the file you are updating.
