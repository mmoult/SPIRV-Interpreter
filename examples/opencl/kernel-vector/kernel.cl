/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
__kernel void vector_fma_kernel(__global const float *input_data,
                                __global const float *mul_vec,
                                __global const float *add_vec,
                                __global float4 *out) {
    int id = get_global_id(0);

    // 1. vload4 loads 4 contiguous floats starting at (input_data + id * 4) as a float4
    float4 data_block = vload4(id, input_data);

    // 2. Load our multiplier and adder vectors
    float4 m = vload4(id, mul_vec);
    float4 a = vload4(id, add_vec);

    // 3. fma performs a fused multiply-add on vector types: (data_block * m) + a
    float4 result = fma(data_block, m, a);

    // Write out the result vector directly
    out[id] = result;
}
