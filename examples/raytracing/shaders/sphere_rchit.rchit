#version 460 core
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT accelerationStructure;

layout(location = 0) rayPayloadInEXT vec3 payload;
layout(location = 1) rayPayloadEXT vec3 reflectionPayload;

hitAttributeEXT vec3 normal;

const float EPSILON_BIAS = 0.001;

void main()
{
    vec3 point_light = vec3(0.0, 10.0, 0.0); // World-space
    vec3 light_color = vec3(1.0);
    vec3 object_color = vec3(0.83, 0.69, 0.22);
    vec3 intersection_point = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 light_dir = normalize(intersection_point - point_light); // dst - src
    vec3 dir_to_light = -light_dir;

    // Ambient
    vec3 ambient = light_color * 0.1;

    // Diffuse
    float kd = 1.0;
    float diffuse = kd * max(dot(dir_to_light, normal), 0.0);

    // Specular
    float ks = 0.75;
    uint shininess = 10;
    vec3 view_dir = -gl_WorldRayDirectionEXT;
    vec3 reflect_dir = normalize(2.0 * dot(dir_to_light, normal) * normal - dir_to_light);
    float specular = ks * pow(max(dot(view_dir, reflect_dir), 0.0), shininess);

    // Distance attenuation
    float dist = distance(point_light, intersection_point);
    const float light_constant = 0.3;
    const float light_linear = 0.075;
    const float light_quadratic = 0.0;
    float dist_attenuation = 1.0 / (light_constant + light_linear * dist + light_quadratic * dist * dist);

    vec3 color = dist_attenuation * (ambient + (light_color * (diffuse + specular))) * object_color;

    // Shoot a reflection ray
    const uint reflection_ray_flags = gl_RayFlagsCullBackFacingTrianglesEXT;
    traceRayEXT(
        accelerationStructure, 
        reflection_ray_flags, 
        0x1, 
        0, 
        0, 
        0, 
        intersection_point + normal * EPSILON_BIAS, 
        gl_RayTminEXT, 
        reflect_dir, 
        gl_RayTmaxEXT, 
        1
    );
    const float reflection_constant = 0.2;
    color += reflectionPayload * reflection_constant;

    payload = color;
}
