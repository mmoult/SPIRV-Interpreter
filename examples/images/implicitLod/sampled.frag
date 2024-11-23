/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* Modified version of https://github.com/SaschaWillems/Vulkan/blob/master/shaders/glsl/imgui/ui.frag
 * Licensed with:
 *   The MIT License (MIT)
 *   Copyright (c) 2016 Sascha Willems
 */
#version 450

layout (binding = 0) uniform sampler2D fontSampler;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main()
{
	outColor = inColor * texture(fontSampler, inUV);
}
