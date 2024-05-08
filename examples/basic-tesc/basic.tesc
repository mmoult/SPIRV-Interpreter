#version 450
layout(vertices = 1) out;

layout(location = 0) patch out struct {
    vec4 bez;
} pv;

void main()
{
    pv.bez = vec4(gl_in[0].gl_Position.x, gl_in[1].gl_Position.x, gl_in[2].gl_Position.x, gl_in[3].gl_Position.x);
    float factor = 1.0;
    gl_TessLevelInner[0] = factor;
    gl_TessLevelInner[1] = factor;
    factor = 2.0;
    gl_TessLevelOuter[0] = factor;
    gl_TessLevelOuter[1] = factor;
    gl_TessLevelOuter[2] = factor;
    gl_TessLevelOuter[3] = factor;
}
