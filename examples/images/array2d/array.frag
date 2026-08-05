#version 450
// A 2D array image: layers are addressed, never blended between, and each layer carries its own mipmap chain.
layout(set = 0, binding = 0) uniform sampler2DArray Tex;
layout(location = 0) flat in vec3 coord;  // xy within a layer, z the layer
layout(location = 1) flat in int lod;
layout(location = 0) out vec4 color;
layout(location = 1) out ivec4 size;

void main() {
    color = textureLod(Tex, coord, float(lod));
    // The array length follows the spatial axes and is not divided by the level of detail.
    size = ivec4(textureSize(Tex, lod), 0);
}
