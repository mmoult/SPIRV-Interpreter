#version 460 core
#extension GL_EXT_ray_tracing : require

layout(location = 2) rayPayloadInEXT bool payload;

void main()
{
    payload = false;
}
