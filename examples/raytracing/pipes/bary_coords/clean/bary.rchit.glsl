#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 2, std430) buffer _11_13
{
    vec4 _m0[20];
} _13;

layout(set = 0, binding = 0) uniform accelerationStructureEXT _35;
hitAttributeEXT vec2 _25;

void main()
{
    _13._m0[gl_LaunchIDEXT.x].x = _25.x;
    _13._m0[gl_LaunchIDEXT.x].y = _25.y;
}
