#version 310 es
precision mediump float;

layout(set = 0, binding = 0) uniform mediump sampler2D InputTexture;
layout(location = 0) out vec4 fetched;
layout(location = 1) out vec4 loaded;

void main()
{
    vec2 floor_coords = floor(gl_FragCoord.xy);
    mediump ivec2 fetchPos = ivec2(floor_coords);
    fetched = texelFetch(InputTexture, fetchPos, 1);
    loaded = textureLod(InputTexture, gl_FragCoord.xy, 0.5);
}
