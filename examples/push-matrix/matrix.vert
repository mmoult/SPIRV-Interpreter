/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(location = 0) in vec3 perspective;
layout(location = 0) out vec4 out_color;
layout(location = 1) out Mats {
	mat4 mat_mul;
	mat4 el_mul;
};

layout(push_constant) uniform PC
{
	mat4 render_mat;
	mat4 other_mat;
	vec4 data; // unused
} push_data;

void main()
{
	mat_mul = push_data.render_mat * push_data.other_mat;
	el_mul = matrixCompMult(push_data.render_mat, push_data.other_mat);
	mat4 sum = (mat_mul + el_mul) * 5.39 - mat_mul;
	out_color = sum * vec4(perspective, 1.0);
}
