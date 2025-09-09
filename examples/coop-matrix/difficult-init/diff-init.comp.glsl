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

const uint ROWS = 3;
const uint COLS = 2;
const uint SIZE = ROWS * COLS;

const uint START = 0;
const uint STRIDE = 1;

layout(set = 0, binding = 0) coherent buffer Block16 {
    float x[SIZE];
    float y[SIZE];
} block;

// Even though this is in static scope, because it is a thread-based cooperative value variable, each invocation will
// have a different value here
coopmat<float, gl_ScopeSubgroup, ROWS, COLS, gl_MatrixUseA> amat;

void main() {
    for (uint i = 0; i < amat.length(); ++i) {
        // It is undefined behavior to rely on the specific distribution method for the cooperative matrix, but we do
        // this only for debugging / testing purposes.
        amat[i] = float(i);
    }
    coopMatStore(amat, block.x, START, STRIDE, gl_CooperativeMatrixLayoutRowMajor);

    coopmat<float, gl_ScopeSubgroup, ROWS, COLS, gl_MatrixUseA> bmat =
        coopmat<float, gl_ScopeSubgroup, ROWS, COLS, gl_MatrixUseA>(1.5);
    for (uint i = 0; i < bmat.length(); ++i) {
        bmat[i] += 1.0;
    }
    coopMatStore(bmat, block.y, START, STRIDE, gl_CooperativeMatrixLayoutRowMajor);
}
