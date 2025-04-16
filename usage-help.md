# Usage Help

Use `-h` or `--help` on the `spirv-run` executable to see the definitive list of command line arguments and flags.
However, some of the more complex options have extra information here.

## Input

### Template generation

The interpreter can automatically generate template files which can be filled in with desired values before being used
as inputs. In the simplest case, you can do:

```
spirv-run -t in.yaml spv_file
```

to generate a template file, `in.yaml`, for the given SPIR-V file, `spv_file`. Each variable which can be given as input
for the SPIR-V file will have its name appear in the template file assigned to some stub value.

A stub value matches the expected type of the value to which it is assigned. Array variables have stub array values,
recursively composed of stub values for each element. Similarly, object variables have stub object values, recursively
composed of stub values for each of its fields. Stub primitives match the form:

```
< primitive-type >
```

There are five main stub primitives, corresponding to each of the five primitive types:

- floating point number, a.k.a. `float` -> `<float>`
- signed integer, a.k.a. `int` -> `<int>`
- unsigned integer, a.k.a. `uint` -> `<uint>`
- Boolean value, a.k.a. `bool` -> `<bool>`
- string character sequence, a.k.a. `string` -> `<string>`

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
Run `spirv-run -r closest.json rchit.spv` and `spirv-run -r miss.json rmiss.spv` (replacing the dummy names shown with
the actual file paths and desired template outputs).
2. Generate an initial template for the main shader stage. This will give you a template with all input variables when
set to default specialization constant values (if any specialization constants) and an empty shader binding table (for
raytracing only). Replace stubs with actual values.
3. Generate *another* template file, using the filled template generated in step 2 as an input. For example, do
`spirv-run -i first.json -t in.json spv_file`. It is recommended to match the file formats between the initial template
and the desired final template.
4. Fill in the new template file, especially any new or changed values. If a variable appears with the same stub for its
value in the final and original template, it is safe to copy the value from the initial template directly into the final
template (this is why matching the template file formats is recommended- it makes copying easier).
5. Use what was the final template file as an input to an interpreter run.

## Output

This section is in development. Come back later.

## Program Control

This section is in development. Come back later.
