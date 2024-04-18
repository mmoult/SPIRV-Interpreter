/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 420

layout(std140, binding = 1) uniform BlockInfo {
    vec4 blockColor;
};
layout(location = 1) flat in ivec2 coords;
layout(location = 0) out vec4 diffuseColor;

vec4 ray = vec4(0.12, 3.45, 6.78, 9.01);

float directionalComp(vec4 relative, float divisor) {
    float ret = dot(ray, relative) / divisor;
    if (-ret > 0)
        discard;
    return ret;
}

void main() {
    diffuseColor = blockColor;
    for (int i = 0; i < 4; ++i) {
        int x = coords.x * coords.y;

        if (x >= i)
            diffuseColor -= directionalComp(diffuseColor, float(x));
    }
}
