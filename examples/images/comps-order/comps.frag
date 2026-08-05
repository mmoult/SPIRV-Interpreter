#version 450
// A sampled image has no Image Format, so the input file is what says which channels are present and in what order.
// Whatever order it uses, a texel has to reach the shader as RGBA.
layout(set = 0, binding = 0) uniform sampler2D Tex;
layout(location = 0) out vec4 color;

void main() {
    color = texture(Tex, vec2(0.0, 0.0));
}
