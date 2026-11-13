# Usage Help

Use `-h` or `--help` on the `spirv-run` executable to see the definitive list of command line arguments and flags.
This file expands upon some of the more complex options by giving extra information.

## Input

SPIR-V shaders / kernels typically require input data before execution. Some options exist to facilitate this collection
of these input values.

### Template generation

The interpreter can automatically generate template files which can be filled in with desired values before being used
as inputs. In the simplest case, you can do:

```
spirv-run -t in.yaml shader.spv
```

to generate a template file, `in.yaml`, for the given SPIR-V file, `shader.spv`. Each variable which can be given as
input for the SPIR-V file will have its name appear in the template file assigned to some stub value.

A stub value matches the expected type of the value to which it is assigned. Array variables have stub array values,
recursively composed of stub values for each element. Similarly, object variables have stub object values, recursively
composed of stub values for each of its fields. Stub primitives match the form:

```
< primitive-type >
```

There are five main stub primitives, corresponding to each of the five primitive types:

- `<float>`  -- floating point number, such as `3.14`, `-0.81`, or `42.0`
- `<int>`    -- signed integer, such as `-916` or `7`
- `<uint>`   -- unsigned integer, such as `3` or `582`
- `bool`     -- Boolean value, corresponding to either `true` or `false`
- `<string>` -- string character sequence using `"double quotes"`

Additionally, there is a special sixth stub primitive, `<...>`, which is used in arrays to indicate that the previous
value may be repeated 0 or more times. This is used to handle "runtime arrays" in SPIR-V, where the size of an array is
indeterminate before the runtime data is presented.

Some shaders require some special steps to properly generate the template file:
- shaders with specialization constants: the values of *all* specialization constants are required before any of the
  regular inputs are handled. Sometimes, the length of input array(s) is determined by specialization constants. Thus,
  an input with specialization constants is required before the total template can be output.
- shaders with shader binding table (for raytracing) data: the intepreter must load all other shaders in the SBT before
  it can generate a final list of all inputs. Therefore, the shader binding table must be populated before the final
  template can be output.
- raytracing substages with shader record variables: these values are expected in a separate extra input file for any
  shader binding table entries needed. You can use command line option `-r` to generate a special substage template.

In summary, here is a comprehensive list of steps which may be taken to guarantee correct and total template generation:
1. For all raytracing substages, generate extra input files. For example, consider a raytracing pipeline with an rgen
shader, closest hit shader, and miss shader. The closest hit and miss shaders are substages and rgen is the main stage.
Run `spirv-run -r -t closest.json rchit.spv` and `spirv-run -r -t miss.json rmiss.spv` (replacing the dummy names shown
with the actual file paths and desired template outputs).
2. Generate an initial template for the main shader stage. This will give you a template with all input variables when
set to default specialization constant values (if any specialization constants) and an empty shader binding table (for
raytracing only). Replace stubs with actual values.
3. Generate *another* template file, using the filled template generated in step 2 as an input. For example, do
`spirv-run -i first.json -t in.json shader.spv`. It is recommended to match the file formats between the initial template
and the desired final template.
4. Fill in the new template file, especially any new or changed values. If a variable appears with the same stub for its
value in the final and original template, it is safe to copy the value from the initial template directly into the final
template (this is why matching the template file formats is recommended- it makes copying easier).
5. Use what was the final template file as an input to an interpreter run.

## Output

Data is printed in the "default format". This is decided by matching the given output file (.yaml or .json), if present.
If not, the format can be given with the `--format` option.

### Discard

A fragment shader invocation may discard its fragment by terminating (with `OpKill` or `OpTerminateInvocation`) or by
demoting to a helper invocation (`OpDemoteToHelperInvocation`). If a shader contains any of these instructions, the
output has an extra Boolean named `@discarded`, which is `true` if the invocation was terminated or demoted and `false`
if it was not. Shaders which cannot discard do not have the extra output.

When checking with `--check`, `@discarded` is compared only if it appears in the check file. Check files which do not
mention it are unaffected.

SPIR-V alone does not say what happens to the output variables of a discarded invocation: they hold whatever they were
assigned before the discard. By default, they are checked like any other output. Some APIs do not write the outputs of
a discarded fragment (see [API](#api)). Under those, variables with the Output storage class are left unchecked when
the invocation was discarded, and they need not appear in the check file. Buffers and images are always checked, since
what the invocation wrote to them before it was discarded is unaffected.

## API

SPIR-V defines the rules common to every environment. Each API that consumes SPIR-V builds on them with rules of its
own, so the rules of SPIR-V are a subset of the rules of any API. The interpreter applies the rules of SPIR-V by
default. The `--api` option also applies the rules of an API. The API is one of `opencl`, `opengl`, and `vulkan`, and
may be abbreviated as long as the abbreviation is not ambiguous (`vul` is Vulkan, but `op` could be OpenCL or OpenGL).
The extra rules of each API follow, with links to the specifications they come from.

### OpenCL

None yet. The rules for SPIR-V modules in OpenCL are the
[validation rules](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_Env.html#validation-rules) of the
OpenCL environment specification.

### OpenGL

The output variables of a discarded fragment are not checked (see [Discard](#discard)). This follows the
[`discard` statement](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html#jumps) of the OpenGL Shading
Language specification. The SPIR-V environment is described in
[ARB_gl_spirv](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gl_spirv.txt).

### Vulkan

The output variables of a discarded fragment are not checked (see [Discard](#discard)). This follows
[Shader Termination and Demotion](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fragops-shader-termination)
in the Vulkan specification.

## Printing

Enables the printing of an execution traces. Instructions are printed as they are executed with their result data.

## Quiet

Disables the printing of runtime warnings to standard error. These warnings may be useful to flag undefined behavior,
such as bound disordering, division by zero, and unexpected underflow/overflow, but their silencing is available for
user convenience.

## Program Control

### Timeout

SPIR-V programs may contain infinite loops, especially for unintended inputs. When invoking the interpreter, it may be
undesirable for the execution to extend beyond an anticipated runtime. This is often the case when running testing
scripts- an infinite loop would prevent the execution from being calculated into the final result.

Using the `-T` or `--timeout` option, the user may specify a fixed number of dynamic instruction executions for the
program not to exceed, given as a power of 10. For example, 1 is 10^1 = 10 instructions, 2 is 10^2 = 100 instructions,
and so on.

A dynamic instruction execution is a single instance on a single invocation. Therefore, if the same instruction is
repeated several times in a program, it will be counted multiple times for the dynamic instruction count. If multiple
invocations run an instruction, the instruction will be counted for each.

### Invocation Scheduling

The order in which invocations are executed may be customized. The SPIR-V specification requires no particular ordering
of these invocations (outside of explicit synchronization points), and in fact, it may be advantageous to test different
orderings as an assurance that no data races are present.

There are 3 scheduling modes: round-robin, sequential, and random.

* Round Robin: each invocation is stepped once, in ascending order, until all invocations complete. For example: 0, 1,
2, 3, ... N - 1, 0, 1, 2, ...
* Sequential: each invocation is run to completion before running the next, processed in ascending order until all
invocations complete. For example: 0. 1. 2. 3. ... N - 1
* Random: a random uncompleted invocation is selected and stepped once before selecting another random invocation, until
all invocations complete. For example: 3, 9, 2, ...

If a custom order pattern is given, it takes precedent over the selected mode. This pattern must follow a set of
particular syntax rules to be understood by the scheduler:

* A single step of an invocation is represented by its index. "0", "5", "31", etc
* A comma may be used to separate steps of multiple invocations. "0,1,2". Note that repetitions are legal: "7,7,4,7"
* Spaces have no semantic meaning and are ignored. "6, 3, 2" = "6,3,2"
* Periods serve as a sequencing operator. The pattern prefix prior to the period is repeated to completion. For example,
"0." runs invocation 0 to completion before running any other. "9,0." runs invocations 9 and 0 to completion before
running any other, alternating a single step between the two, starting with 9.
* There are two kinds of ranges: separated by commas and separated by periods. The type is indicated after a comma
separator: "0:,3" and "4:.9". The start and stop invocations are inclusive. The start does not need to be a minimum and
the end a maximum; "6:,3" is the same as "6,5,4,3".
* A step value may be included after a colon separator at the end of any range. "4:,9:2". The step value must be a
positive integer, or in other words, must be >= 1. The direction of the step matches the range bounds, therefore,
"7:.1:2" is the same as "7.5.3.1"

The pattern is consumed by the scheduler as it is used. That is to say, used sections of the pattern are deleted and
reaching the end of the pattern will *not* loop to the beginning.

A pattern cannot be used to run an invocation that is blocked or completed. If such an invocation appears, it is skipped
and removed from the pattern.
