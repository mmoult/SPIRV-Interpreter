/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460
#extension GL_EXT_terminate_invocation : require

layout(location = 0) in float x;

// uses OpTerminateInvocation instead of OpKill
void main() {
    if (x < 0.0)
        terminateInvocation;
}
