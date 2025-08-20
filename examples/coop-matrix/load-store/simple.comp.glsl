/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450 core
#extension GL_KHR_memory_scope_semantics : enable
#extension GL_KHR_cooperative_matrix : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_buffer_reference : enable

layout(local_size_x = 4, local_size_y = 1, local_size_z = 1) in;

const uint ROWS = 8;
const uint COLS = 2;
const uint SIZE = ROWS * COLS;

const uint START = 0;
const uint STRIDE = 1;

layout(set = 0, binding = 0) coherent buffer Block16 {
    float x[SIZE];
} block;

void main() {
    coopmat<
        float, // element type
        // Scope: array elements are spread between invocations in the scope in an implementation-dependent manner.
        gl_ScopeSubgroup,
        ROWS,
        COLS,
        // MatrixUse: which determines which operand in coopMatMulAdd the type can be used as:
        // 0 - gl_MatrixUseA
        // 1 - gl_MatrixUseB
        // 2 - gl_MatrixUseAccumulator
        gl_MatrixUseAccumulator
    > m;
    coopMatLoad(m, block.x, START, STRIDE, gl_CooperativeMatrixLayoutColumnMajor);
    coopMatStore(m, block.x, START, STRIDE, gl_CooperativeMatrixLayoutRowMajor);
}
