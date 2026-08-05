#version 450
// A 3D image: unlike a 2D array, the third axis is a real axis and is interpolated along.
layout(set = 0, binding = 0) uniform sampler3D Tex;
layout(location = 0) flat in vec3 coord;
layout(location = 0) out vec4 color;

void main() {
    color = texture(Tex, coord);
}
