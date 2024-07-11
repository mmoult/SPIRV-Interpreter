#version 460 core
#extension GL_EXT_ray_query : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT TLAS;

layout(location = 0) flat in uint rayFlags;
layout(location = 1) flat in uint cullMask;
layout(location = 2) in vec3 rayOrigin;
layout(location = 3) in float rayTMin;
layout(location = 4) in vec3 rayDirection;
layout(location = 5) in float rayTMax;

layout(location = 6) out vec3 outColor;

void main()
{
    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, TLAS, rayFlags, cullMask, rayOrigin, rayTMin, rayDirection, rayTMax); // TODO: causing exit 1

    rayQueryGetRayTMinEXT(rayQuery);
    rayQueryGetRayFlagsEXT(rayQuery);
    rayQueryGetWorldRayOriginEXT(rayQuery);
    rayQueryGetWorldRayDirectionEXT(rayQuery);

    outColor = vec3(0.0);

    while (rayQueryProceedEXT(rayQuery)) {}
    
    rayQueryTerminateEXT(rayQuery); // TODO: causing exit 1
}
