#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 1, std140) uniform _14_16
{
    vec4 _m0[20];
} _16;

layout(set = 0, binding = 2, std430) buffer _43_45
{
    vec4 _m0[20];
} _45;

layout(set = 0, binding = 0) uniform accelerationStructureEXT _32;
layout(location = 0) rayPayloadEXT vec3 _41;

void main()
{
    vec3 _9 = _16._m0[gl_LaunchIDEXT.x].xyz;
    traceRayEXT(_32, 0u, 255u, 0u, 0u, 0u, vec3(0.0), 0.0, _9, 1.0, 0);
}
