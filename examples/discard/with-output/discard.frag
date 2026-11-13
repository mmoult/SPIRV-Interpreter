/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460

layout(location = 0) in float x;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) buffer Data {
    float written;
} data;

void main() {
    // Both of these happen before any discard
    data.written = x;
    outColor = vec4(x);
    if (x < 0.0)
        discard;
    outColor = vec4(7.0);
}
