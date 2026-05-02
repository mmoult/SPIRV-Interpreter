#version 450 core
#extension GL_ARB_gpu_shader_int64 : require

layout(location = 0) out float fragcolor;
layout(location = 0) flat in int dummy_input; // Input defined but not used

void main() {
    int64_t a = 4294967298L; // 2^32 + 2
    fragcolor = float(a);
}
