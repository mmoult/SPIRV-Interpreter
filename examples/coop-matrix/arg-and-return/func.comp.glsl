/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450 core
#pragma use_vulkan_memory_model
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_memory_scope_semantics : enable
#extension GL_KHR_cooperative_matrix : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_NV_cooperative_matrix2 : enable

layout(constant_id = 1) const int M = 8;
layout(constant_id = 2) const int N = 8;
const int SIZE = M * N;

layout(local_size_x_id = 0, local_size_y = 4, local_size_z = 1) in;
layout(set=0, binding=0) coherent buffer Buf {
    float16_t x[SIZE];
} values;

coopmat<float16_t, gl_ScopeSubgroup, M, N, gl_MatrixUseA> matA;
coopmat<float16_t, gl_ScopeSubgroup, M, N, gl_MatrixUseA> matO;

coopmat<float16_t, gl_ScopeSubgroup, M, N, gl_MatrixUseA>
func(coopmat<float16_t, gl_ScopeSubgroup, M, N, gl_MatrixUseA> m)
{
    return -m;
}

void main()
{
    /*
    uvec2 subgroupXY = uvec2(gl_SubgroupID % gl_NumSubgroups.x, gl_SubgroupID / gl_NumSubgroups.x);
    uvec2 matrixID = uvec2(gl_WorkGroupID.xy) * gl_NumSubgroups + subgroupXY;
    // Fetch subgroup location data from (Y, X) in the input
    uint start = N * M * matrixID.y + N * M * gl_NumSubgroups.y * matrixID.x;
    */
    uint start = 0;

    coopMatLoad(matA, values.x, start, N, gl_CooperativeMatrixLayoutRowMajor);
    matO = func(matA);
    coopMatStore(matO, values.x, start, N, gl_CooperativeMatrixLayoutRowMajor);
}
