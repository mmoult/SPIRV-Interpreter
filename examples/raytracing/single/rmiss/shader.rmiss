#version 460 core
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

void main()
{
    // Populate the payload with a color that depends on the ray's direction in world space
    hitPayload = vec3(normalize(abs(gl_WorldRayDirectionEXT)));
}
