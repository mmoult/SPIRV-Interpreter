#version 460

layout(location = 0) out vec3 outColor;

void main() {
    outColor = gl_FragCoord.xyz;
}
