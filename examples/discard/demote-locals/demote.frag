/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460
#extension GL_EXT_demote_to_helper_invocation : require

layout(location = 0) out vec4 outColor;

void add_to(inout float value) {
    value += 1.0;
}

void main() {
    outColor = vec4(1.0);
    float acc = 0.0;
    demote;
    // The invocation is a helper now, but it still runs, so its own variables must still update, whether directly or
    // through a pointer. If they were not stored, this loop would never finish.
    int i = 0;
    while (i < 4) {
        ++i;
        add_to(acc);
    }
    while (acc < 4.0)
        add_to(acc);
    // The output of a demoted invocation is discarded
    outColor = vec4(acc);
}
