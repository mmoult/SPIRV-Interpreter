/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(constant_id = 0) const bool add = true;
layout(location = 0) in vec4 foo;
layout(location = 1) in vec4 bar;

void main()
{
    bool result = add == true;
    if (result)
        gl_Position = foo + bar;
    else
        gl_Position = foo - bar;
}
