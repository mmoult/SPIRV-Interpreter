/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460
#extension GL_EXT_ray_tracing : enable

struct HitInfo
{
   uint value;      // Attribute value of the closest hit
   uint primitive;  // Primitive of the closest hit
   float distance;  // Distance of the closest hit
};
layout(location = 0) rayPayloadInEXT HitInfo payload;
hitAttributeEXT uint value;

void main()
{
   payload.value = value;
   payload.primitive = gl_PrimitiveID;
   payload.distance = gl_RayTmaxEXT;
}
