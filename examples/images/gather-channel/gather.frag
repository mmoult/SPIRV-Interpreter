#version 460
// textureGather names one of the four components. The image need not carry it: component 3 of a two-channel image is an
// alpha that is not stored, and reads as one for all four texels of the footprint.
layout(set = 0, binding = 0) uniform sampler2D Tex;
layout(location = 0) out vec4 green;
layout(location = 1) out vec4 alpha;

void main() {
    green = textureGather(Tex, vec2(0.0, 0.0), 1);
    alpha = textureGather(Tex, vec2(0.0, 0.0), 3);
}
