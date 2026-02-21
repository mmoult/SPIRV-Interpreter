// Recursion is allowed in SPIR-V, but not in other front-end shader languages
// (GLSL, Slang, HLSL). This is a SPIR-V interpreter, so we should support all
// potential use cases of SPIR-V, not just Vulkan.
//
//   VUID-StandaloneSpirv-None-04634
//   The static function-call graph for an entry point must not contain cycles;
//   that is, static recursion is not allowed
//
// This file is an approximate description of what the `recursive.spv` test
// does. It exists only to help the reader understand the test.
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std140, binding = 1) buffer Struct {
    int m0; // input
    int m1; // modifiable and accessible by recursive function
    int m2; // output
};

int recursive(int depth) {
    if (depth < 2)
        return 1;

    // Do NOT use tail-call to force actual recursion to occur!
    int next = recursive(depth - 1);

    if (m1 <= 0)
        m1 += next;
    else
        m1 -= next;

    return next + depth;
}

void main() {
    m2 = recursive(m0);
}
