#version 460 core
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT accelerationStructure;

layout(location = 0) rayPayloadInEXT vec3 payload;
layout(location = 1) rayPayloadEXT vec3 reflectionPayload;
layout(location = 2) rayPayloadEXT bool shadowPayload;

const float EPSILON_BIAS = 0.001;

void main() {
    const vec3 intersection_point = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 object_color;
    vec3 normal;
    if (gl_InstanceCustomIndexEXT == 0)  // Ceiling
    {
        object_color = vec3(1.0);
        normal = vec3(0.0, -1.0, 0.0);
    }
    else if (gl_InstanceCustomIndexEXT == 1)  // Floor
    {
        const float tile_size = 2.5;
        const float total = floor(intersection_point.x / tile_size) + floor(intersection_point.z / tile_size);
        if (mod(total, 2.0) == 0.0)
        {
            object_color = vec3(0.2);
        }
        else 
        {
            object_color = vec3(1.0);
        }
        normal = vec3(0.0, 1.0, 0.0);
    }
    else if (gl_InstanceCustomIndexEXT == 2)  // Red wall
    {
        object_color = vec3(1.0, 0.0, 0.0);
        normal = vec3(1.0, 0.0, 0.0);
    }
    else if (gl_InstanceCustomIndexEXT == 3)  // Green wall
    {
        object_color = vec3(0.0, 1.0, 0.0);
        normal = vec3(-1.0, 0.0, 0.0);
    }
    else if (gl_InstanceCustomIndexEXT == 4)  // Blue wall
    {
        object_color = vec3(0.0, 0.0, 1.0);
        normal = vec3(0.0, 0.0, -1.0);
    }
    else if (gl_InstanceCustomIndexEXT == 5)  // Orange wall
    {
        object_color = vec3(1.0, 0.5, 0.3);
        normal = vec3(0.0, 0.0, 1.0);
    }
    else
    {
        object_color = vec3(0.0);
        normal = vec3(0.0);
    }

    vec3 point_light = vec3(0.0, 10.0, 0.0); // World-space
    vec3 light_color = vec3(1.0);
    vec3 light_dir = normalize(intersection_point - point_light); // dst - src
    vec3 dir_to_light = -light_dir;

    // Ambient
    vec3 ambient = light_color * 0.3;

    // Diffuse
    float kd = 0.4;
    float diffuse = kd * max(dot(dir_to_light, normal), 0.0);

    // Specular
    float ks = gl_InstanceCustomIndexEXT == 5 ? 0.75 : 0.04;
    uint shininess = 10;
    vec3 view_dir = -gl_WorldRayDirectionEXT;
    vec3 reflect_dir = reflect(light_dir, normal);  // Phong
    vec3 half_dir = normalize(dir_to_light + view_dir);  // Blinn-phong
    float specular = ks * pow(max(dot(view_dir, half_dir), 0.0), shininess);

    // Distance attenuation
    float dist = distance(point_light, intersection_point);
    const float light_constant = 0.3;
    const float light_linear = 0.075;
    const float light_quadratic = 0.0;
    float dist_attenuation = 1.0 / (light_constant + light_linear * dist + light_quadratic * dist * dist);

    vec3 color = dist_attenuation * (ambient + (light_color * (diffuse + specular))) * object_color;

    // Shoot a shadow ray
    const uint shadow_ray_flags = gl_RayFlagsCullBackFacingTrianglesEXT | gl_RayFlagsSkipClosestHitShaderEXT;
    shadowPayload = true;
    traceRayEXT(
        accelerationStructure, 
        shadow_ray_flags, 
        0xFF, 
        0, 
        0, 
        1, 
        intersection_point + normal * EPSILON_BIAS, 
        gl_RayTminEXT, 
        dir_to_light, 
        distance(intersection_point, point_light) - 1.0, 
        2
    );
    // "shadowPayload" will be false if the shadow miss shader is invoked
    if (shadowPayload) // True if the shadow intersected an object on the way to the light source
    {
        color *= 0.3;
    }

    // Make the orange wall reflective
    if (gl_InstanceCustomIndexEXT == 5)
    {
        // Shoot a reflection ray
        const uint reflection_ray_flags = gl_RayFlagsCullBackFacingTrianglesEXT;
        traceRayEXT(
            accelerationStructure, 
            reflection_ray_flags, 
            0x2, 
            0, 
            0, 
            0, 
            intersection_point + normal * EPSILON_BIAS, 
            gl_RayTminEXT, 
            reflect_dir, 
            gl_RayTmaxEXT, 
            1
        );
        const float reflection_ratio = 0.3;  // Out of 1.0
        color = (color * (1.0 - reflection_ratio)) + (reflectionPayload * reflection_ratio);
    }

    payload = color;
}
