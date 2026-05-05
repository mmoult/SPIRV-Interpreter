/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450
#extension GL_AMD_gpu_shader_half_float : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 1, std140) buffer Output
{
    f16vec4 step1;
    f16vec2 step2;
    float16_t step3;
} less;

layout(set = 0, binding = 0, std140) buffer Input
{
    vec4 val;
} full;

void main()
{
    f16vec4 condensed = f16vec4(full.val);
    less.step1 = condensed;
    f16vec2 sum = condensed.xy + condensed.zw;
    less.step2 = sum;
    float16_t quotient = sum.x / sum.y;
    less.step3 = quotient;
}
