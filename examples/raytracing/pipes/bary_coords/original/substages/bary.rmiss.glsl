#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 1, std140) uniform _34_36
{
    vec4 _m0[5];
} _36;

layout(set = 0, binding = 2, std430) buffer _11_13
{
    vec4 _m0[5];
} _13;

layout(set = 0, binding = 0) uniform accelerationStructureEXT _32;
layout(location = 0) rayPayloadInEXT vec3 _29;

void main()
{
    _13._m0[gl_LaunchIDEXT.x] = vec4(-1.0);
}
