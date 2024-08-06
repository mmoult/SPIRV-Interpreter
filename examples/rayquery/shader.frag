#version 460 core
#extension GL_EXT_ray_query : require

// Inputs
layout(set = 0, binding = 0) uniform accelerationStructureEXT TLAS;
layout(set = 1, binding = 0) uniform RayProperties
{
    uint flags;
    uint cullMask;
    vec3 origin;
    vec3 direction;
    float tMin;
    float tMax;
} ray;

// Outputs
layout(location = 0) out vec3 outColor;

// Constants
const float MAX_HIT_T = 1000000000000000000.0;
const float EPSILON_BIAS = 0.001;
const float GROUND_LEVEL = -1.0;

const vec3 LIGHT_COLOR = vec3(0.52, 0.81, 0.92);
const vec3 LIGHT_DIRECTION = vec3(0.0, -1.0, -1.0);

const int PROCEDURAL_SPHERE_0_CUSTOM_INDEX = 0;

const int PROCEDURAL_SPHERE_1_CUSTOM_INDEX = 2;

const int FLOOR_CUSTOM_INDEX = 1;
const vec3 FLOOR_NORMAL = vec3(0.0, 1.0, 0.0);

struct IntersectionProperties
{
    vec3 normal;
    float hit_t;
    bool intersected;
};

struct RayQueryResults
{
    vec3 normal;
};

IntersectionProperties raySphereIntersection(float sphere_radius, vec3 ray_origin, vec3 ray_direction, float ray_t_min, float ray_t_max)
{
    // Assume the ray will miss
    IntersectionProperties result;
    result.normal = vec3(0.0);
    result.hit_t = MAX_HIT_T;
    result.intersected = false;

    vec3 sphere_center = vec3(0.0, 0.0, 0.0);

    // ||ray_o + ray_dir * t||^2 = r^2
    vec3 dist_o_c = ray_origin - sphere_center; // Distance between the ray's origin and the sphere's center
    float a = dot(ray_direction, ray_direction);
    float b = 2.0 * dot(ray_direction, dist_o_c);
    float c = dot(dist_o_c, dist_o_c) - (sphere_radius * sphere_radius);

    float discriminant = (b * b) - (4.0 * a * c);

    if (discriminant >= 0)
    {
        // Set it to the smaller solution
        float hit_t = (-b - sqrt(discriminant)) / (2.0 * a);

        if (hit_t < ray_t_min || hit_t > ray_t_max)
        {
            // Set it to the other solution
            hit_t = (-b - sqrt(discriminant)) / (2.0 * a);
        }

        if (hit_t < ray_t_min || hit_t > ray_t_max)
        {
            // If entered this if-statement, both solutions are bad
            return result;
        }

        result.intersected = true;
        result.hit_t = hit_t;
        result.normal = normalize((ray_origin + ray_direction * result.hit_t) - sphere_center);
    }

    return result;
}

rayQueryEXT rayQuery;
RayQueryResults runRayQuery(float ray_t_min, float ray_t_max)
{
    RayQueryResults results;

    results.normal = vec3(0.0);
    float best_hit_t = MAX_HIT_T;

    while (rayQueryProceedEXT(rayQuery))
    {
        const uint candidate_type = rayQueryGetIntersectionTypeEXT(rayQuery, false);
        if (candidate_type == gl_RayQueryCandidateIntersectionTriangleEXT)
        {
            rayQueryConfirmIntersectionEXT(rayQuery);
            const float triangle_hit_t = rayQueryGetIntersectionTEXT(rayQuery, false);
            if (triangle_hit_t < best_hit_t)
            {
                if (rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false) == FLOOR_CUSTOM_INDEX)
                {
                    results.normal = FLOOR_NORMAL;
                }
                best_hit_t = triangle_hit_t;
            }
        }
        else if (candidate_type == gl_RayQueryCandidateIntersectionAABBEXT)
        {
            const vec3 o = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
            const vec3 d = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);
            const int procedural_custom_index = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
            IntersectionProperties ip;
            if (procedural_custom_index == PROCEDURAL_SPHERE_0_CUSTOM_INDEX ||
                procedural_custom_index == PROCEDURAL_SPHERE_1_CUSTOM_INDEX)
            {
                ip = raySphereIntersection(0.5, o, d, ray_t_min, ray_t_max);
            }

            if (ip.intersected)
            {
                rayQueryGenerateIntersectionEXT(rayQuery, ip.hit_t);
                if (ip.hit_t < best_hit_t)
                {
                    results.normal = ip.normal;
                    best_hit_t = ip.hit_t;
                }
            }
        }
    }

    return results;
}

vec3 getColor(int custom_index, vec3 curr_ray_origin, vec3 curr_ray_direction, vec3 normal)
{
    const float hit_t = rayQueryGetIntersectionTEXT(rayQuery, true);
    vec3 intersection_point = curr_ray_origin + curr_ray_direction * hit_t;

    vec3 color = LIGHT_COLOR;
    vec3 direction_to_light = -LIGHT_DIRECTION;
    if (custom_index == FLOOR_CUSTOM_INDEX) // Floor
    {
        float tile_size = 1.25;
        float total = floor(intersection_point.x / tile_size) + floor(intersection_point.z / tile_size);
        if (mod(total, 2.0) == 0.0)
        {
            color = vec3(0.65, 0.75, 0.4);
        }
        else {
            color = vec3(1.0);
        }
    }
    else if (custom_index == PROCEDURAL_SPHERE_0_CUSTOM_INDEX)
    {
        vec3 ambient = vec3(0.5, 0.0, 0.0);

        float kd = 0.4;
        float diffuse = kd * max(dot(direction_to_light, normal), 0.0);

        float ks = 0.08;
        uint shininess = 5;
        vec3 view_dir = -ray.direction;
        vec3 reflect_dir = (2.0 * dot(direction_to_light, normal) * normal - direction_to_light);
        float specular = ks * pow(max(dot(view_dir, reflect_dir), 0.0), shininess);

        color = ambient + LIGHT_COLOR * (diffuse + specular);
    }
    else if (custom_index == PROCEDURAL_SPHERE_1_CUSTOM_INDEX)
    {
        vec3 ambient = vec3(0.1);

        float kd = 0.0;
        float diffuse = kd * max(dot(direction_to_light, normal), 0.0);

        float ks = 0.05;
        uint shininess = 5;
        vec3 view_dir = -ray.direction;
        vec3 reflect_dir = (2.0 * dot(direction_to_light, normal) * normal - direction_to_light);
        float specular = ks * pow(max(dot(view_dir, reflect_dir), 0.0), shininess);

        color = ambient + LIGHT_COLOR * (diffuse + specular);
    }

    // Shadows
    const vec3 new_ray_pos_offset = normal * EPSILON_BIAS;
    const vec3 new_ray_pos = intersection_point + new_ray_pos_offset;
    rayQueryInitializeEXT(rayQuery, TLAS, ray.flags, ray.cullMask, new_ray_pos, 0.0, direction_to_light, ray.tMax);
    RayQueryResults ray_query_results = runRayQuery(0.0, ray.tMax);

    const uint committed_type = rayQueryGetIntersectionTypeEXT(rayQuery, true);
    if (committed_type != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        color *= vec3(0.15);
    }

    return color;
}

void main()
{
    outColor = LIGHT_COLOR;

    rayQueryInitializeEXT(rayQuery, TLAS, ray.flags, ray.cullMask, ray.origin, ray.tMin, ray.direction, ray.tMax);
    const RayQueryResults ray_query_results = runRayQuery(ray.tMin, ray.tMax);

    uint committed_type = rayQueryGetIntersectionTypeEXT(rayQuery, true);
    if (committed_type == gl_RayQueryCommittedIntersectionNoneEXT)
    {
        return;
    }

    // Material
    int custom_index = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
    outColor = getColor(custom_index, ray.origin, ray.direction, ray_query_results.normal);

    // Reflection (depth of 1)
    if (custom_index == PROCEDURAL_SPHERE_1_CUSTOM_INDEX)
    {
        // Re-run the ray query
        rayQueryInitializeEXT(rayQuery, TLAS, ray.flags, ray.cullMask, ray.origin, ray.tMin, ray.direction, ray.tMax);
        const RayQueryResults ray_query_results = runRayQuery(ray.tMin, ray.tMax);

        uint committed_type = rayQueryGetIntersectionTypeEXT(rayQuery, true);
        if (committed_type == gl_RayQueryCommittedIntersectionNoneEXT)
        {
            return;
        }

        vec3 reflect_dir = (2.0 * dot(-LIGHT_DIRECTION, ray_query_results.normal) * ray_query_results.normal) - (-LIGHT_DIRECTION);
        vec3 new_ray_pos_offset = ray_query_results.normal * EPSILON_BIAS;
        float hit_t = rayQueryGetIntersectionTEXT(rayQuery, true);

        vec3 newRayPos = ray.origin + ray.direction * hit_t + new_ray_pos_offset;
        rayQueryInitializeEXT(rayQuery, TLAS, ray.flags, ray.cullMask, newRayPos, 0.0, reflect_dir, ray.tMax);
        runRayQuery(0.0, ray.tMax);

        committed_type = rayQueryGetIntersectionTypeEXT(rayQuery, true);
        if (committed_type != gl_RayQueryCommittedIntersectionNoneEXT)
        {
            int reflect_custom_index = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
            outColor += getColor(reflect_custom_index, newRayPos, reflect_dir, ray_query_results.normal) * vec3(0.75);
        }
    }
}
