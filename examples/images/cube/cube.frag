#version 450
// A cube map: the coordinate is a direction from the center of the cube, which selects a face and a point on it.
layout(set = 0, binding = 0) uniform samplerCube Tex;
layout(location = 0) flat in vec3 dir;
layout(location = 0) out vec4 color;

void main() {
    color = texture(Tex, dir);
}
