#version 460 core
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

hitAttributeEXT vec3 normal;

void main()
{
    hitPayload *= normal;
}
