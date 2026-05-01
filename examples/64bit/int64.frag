#version 450 core
#extension GL_ARB_gpu_shader_int64 : require

layout(location = 0) out float fragcolor;

void main() {
    int64_t a = 12345678901234L; // 64-bit literal
    fragcolor = float(a);
}
