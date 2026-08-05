#version 450
// An arrayed cube map: the coordinate carries a direction and then the array element, and the two combine into a layer.
layout(set = 0, binding = 0) uniform samplerCubeArray Tex;
layout(location = 0) flat in vec4 dir;  // xyz the direction, w the array element
layout(location = 0) out vec4 color;

void main() {
    color = texture(Tex, dir);
}
