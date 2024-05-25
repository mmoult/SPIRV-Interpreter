/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(constant_id = 0) const int NUM = 4;
layout(location = 1) in Vertex {
    vec4 position;
    vec4 color;
} vertices[NUM];

layout(location = 0) in vec4 anchor;
layout(location = 0) out vec4 res;

void main() {
    vec4 temp;
    temp.xyz = anchor.yzw;
    // Should be able to use the unused channel, although it has undefined value
    vec4 other = temp.wwww;
    if (true)
        other = anchor;
    for (int i = 0; i < NUM; ++i)
        other += other * vertices[i].position * vertices[i].color;
    res = other;
}
