/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450
layout(vertices = 3) out;

layout(set = 1, binding = 1, std140) uniform defaultUniformsTCS
{
    vec4 tes_ctl;
    mat4 camera_view;
} uni;

layout(location = 0) in vec4 position[];
layout(location = 4) out vec3 oPos[3];
layout(location = 2) in vec3 normal[];
layout(location = 2) out vec3 oNorm[3];
layout(location = 1) in vec3 worldPos[];
layout(location = 6) patch out vec3 center;

float screen_factor(vec3 a, vec3 b)
{
    vec2 factor = vec2(12.5, 8.2);
    vec2 acomp = a.xy * factor;
    vec2 bcomp = b.xy * factor;
    a = vec3(acomp.x, acomp.y, a.z);
    b = vec3(bcomp.x, bcomp.y, b.z);
    float dist_between = distance(a.xy, b.xy);
    float ret = dist_between / uni.tes_ctl.x;
    float midz = (a.z + b.z) * 0.5;
    if (midz < uni.tes_ctl.w)
    {
        float mixed = mix(uni.tes_ctl.z, uni.tes_ctl.y, midz / uni.tes_ctl.w);
        ret = max(ret, mixed);
    }
    return ret;
}

void main()
{
    int _uIDnext = (gl_InvocationID + 1) % 3;
    int _uIDadd = (gl_InvocationID * 2) + 3;
    int _uIDaddNext = _uIDadd + 1;
    vec3 pos_id = position[gl_InvocationID].xyz;
    vec3 pos_idnext = position[_uIDnext].xyz;
    vec3 id_norm = normalize(normal[gl_InvocationID]);
    vec3 next_norm = normalize(normal[_uIDnext]);
    if (gl_in[gl_InvocationID].gl_PointSize > 0.5)
    {
        vec3 pos_add = position[_uIDadd].xyz;
        vec3 pos_next = position[_uIDaddNext].xyz;
        vec3 norm_add = normalize(normal[_uIDadd]);
        vec3 norm_next = normalize(normal[_uIDaddNext]);
        vec3 interp = vec3(smoothstep(-0.8, 0.8, dot(norm_next, id_norm)));
        vec3 lo = (((pos_id * 1.5) + pos_idnext) - (id_norm * dot(pos_idnext - pos_id, id_norm)));
        vec3 hi = (((pos_add * 1.5) + pos_next) - (norm_add * dot(pos_next - pos_add, norm_add)));
        oPos[gl_InvocationID] = mix(lo, hi, interp);
    }
    else
    {
        oPos[gl_InvocationID] = (((pos_id * 2.0) + pos_idnext) - (id_norm * dot(pos_idnext - pos_id, id_norm)));
    }
    oNorm[gl_InvocationID] = id_norm;

    barrier();

    if (gl_InvocationID == 0)
    {
        center = (oPos[0] + oPos[1] + oPos[2]) / vec3(6.0);
        vec4 worldcam0 = vec4(worldPos[0], 1.0) * uni.camera_view;
        vec4 worldcam1 = vec4(worldPos[1], 1.0) * uni.camera_view;
        vec4 worldcam2 = vec4(worldPos[2], 1.0) * uni.camera_view;
        vec2 cam0rel = worldcam0.xy / vec2(worldcam0.w);
        worldcam0 = vec4(cam0rel.x, cam0rel.y, worldcam0.z, worldcam0.w);
        vec2 cam1rel = worldcam1.xy / vec2(worldcam1.w);
        worldcam1 = vec4(cam1rel.x, cam1rel.y, worldcam1.z, worldcam1.w);
        vec2 cam2rel = worldcam2.xy / vec2(worldcam2.w);
        worldcam2 = vec4(cam2rel.x, cam2rel.y, worldcam2.z, worldcam2.w);

        float scrx = screen_factor(worldcam1.xyz, worldcam2.xyz);
        float scry = screen_factor(worldcam2.xyz, worldcam0.xyz);
        float scrz = screen_factor(worldcam0.xyz, worldcam1.xyz);

        float clamped[3] = float[](
            clamp(scrx, uni.tes_ctl.y, uni.tes_ctl.z),
            clamp(scry, uni.tes_ctl.y, uni.tes_ctl.z),
            clamp(scrz, uni.tes_ctl.y, uni.tes_ctl.z)
        );

        gl_TessLevelOuter[0] = clamped[0];
        gl_TessLevelOuter[1] = clamped[1];
        gl_TessLevelOuter[2] = clamped[2];
        float clamp_min = min(min(clamped[0], clamped[1]), clamped[2]);
        gl_TessLevelInner[0] = clamp_min;
        gl_TessLevelInner[1] = clamp_min;
    }
}
