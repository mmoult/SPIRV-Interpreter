/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460

layout(std140, binding = 0) uniform BlockInfo {
    float block;
};
layout(location = 0) in flat ivec2 coords;
layout(location = 1, set = 1) in flat float foo;
layout(location = 0) out vec4 color;

void main() {
    color = vec4(
        float(coords.x),
        float(coords.y),
        foo,
        block
    );
}
