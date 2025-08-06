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

layout(set = 0, binding = 0) coherent buffer Block16 {
    float16_t x[SIZE];
} block;

void main() {
    coopmat<float16_t, gl_ScopeSubgroup, ROWS, COLS, gl_MatrixUseA> m1;
    coopmat<float16_t, gl_ScopeSubgroup, ROWS, COLS, gl_MatrixUseA> m2;
    coopMatLoad(m1, block.x, ROWS, COLS, gl_CooperativeMatrixLayoutRowMajor);
    coopMatLoad(m2, block.x, ROWS, COLS, gl_CooperativeMatrixLayoutColumnMajor);

    m1 = m1 + m2;
    m1 = -m1;
    m2 = m2 - m1;
    m2 = m1 / m2;
    m2 = m1 * m2;

    coopMatStore(m2, block.x, ROWS, COLS, gl_CooperativeMatrixLayoutRowMajor);
}
