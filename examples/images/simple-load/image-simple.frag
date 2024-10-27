#version 460

layout(binding = 0, rgba8) readonly uniform image2D uni_txtr;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = imageLoad(uni_txtr, ivec2(gl_FragCoord.xy));
}
