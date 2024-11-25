#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform mediump sampler2D InputTexture;
layout(location = 0) flat in int which;
layout(location = 0) out vec4 color;

void main() {
    switch (which % 3) {
    case 0: {
        vec2 floor_coords = floor(gl_FragCoord.xy);
        mediump ivec2 fetchPos = ivec2(floor_coords);
        color = texelFetch(InputTexture, fetchPos, 1);
        break;
    }
    case 1:
        color = textureLod(InputTexture, gl_FragCoord.xy, 0.5);
        break;
    default:
        color = fract(gl_FragCoord);
        color.a = 1.0 - color.a;
        break;
    }
}
