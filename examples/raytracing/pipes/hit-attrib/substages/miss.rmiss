/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* Modified version of https://github.com/SaschaWillems/Vulkan/blob/master/shaders/glsl/raytracingbasic/miss.rmiss
 * Licensed with:
 *   The MIT License (MIT)
 *   Copyright (c) 2016 Sascha Willems
 */
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	hitValue = vec3(0.0, 0.0, 0.2);
}