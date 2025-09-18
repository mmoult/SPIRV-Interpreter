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

// intentionally more threads than there are elements in the matrix
layout(local_size_x = 5, local_size_y = 1, local_size_z = 1) in;

const uint ROWS = 2;
const uint COLS = 2;
const uint SIZE = ROWS * COLS;
const uint START = 0;

layout(set = 0, binding = 0) coherent buffer Block16 {
    float x[SIZE];
    float y[SIZE];
    float z[SIZE];
} block;

void main() {
    coopmat<float, gl_ScopeSubgroup, ROWS, COLS, gl_MatrixUseA> a;
    coopmat<float, gl_ScopeSubgroup, COLS, ROWS, gl_MatrixUseB> b;
    coopmat<float, gl_ScopeSubgroup, ROWS, ROWS, gl_MatrixUseB> c;
    coopMatLoad(a, block.x, START, COLS, gl_CooperativeMatrixLayoutRowMajor);
    coopMatLoad(b, block.y, START, ROWS, gl_CooperativeMatrixLayoutRowMajor);
    coopMatLoad(c, block.z, START, ROWS, gl_CooperativeMatrixLayoutRowMajor);
    c = coopMatMulAdd(a, b, c);
    coopMatStore(c, block.z, START, ROWS, gl_CooperativeMatrixLayoutRowMajor);
}
