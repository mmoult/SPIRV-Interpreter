/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 bPos;
layout(location = 0) out vec4 vertexColor;

void main()
{
    gl_Position = vec4(aPos, 1.0);
    vec4 tmp = gl_Position * 0.42 + normalize(bPos);
    vertexColor = vec4(0.5, 0.0, tmp.xy);
}
