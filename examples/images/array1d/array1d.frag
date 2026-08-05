#version 450
// A 1D array image. Only one axis is spatial, so the array length lands where a height would otherwise be.
layout(set = 0, binding = 0) uniform sampler1DArray Tex;
layout(location = 0) flat in vec2 coord;  // x within a layer, y the layer
layout(location = 0) out vec4 color;
layout(location = 1) out ivec4 size;

void main() {
    color = texture(Tex, coord);
    size = ivec4(textureSize(Tex, 0), 0, 0);
}
