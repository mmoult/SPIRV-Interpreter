/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 460
#extension GL_EXT_ray_tracing : enable

struct HitInfo
{
    vec3 weights;    // Barycentric weights of the closest hit
    float distance;  // Distance of the latest accepted hit
    uint hits;       // Number of hits accepted
    uint primitive;  // Primitive of the closest hit
};
layout(location = 0) rayPayloadInEXT HitInfo payload;
hitAttributeEXT vec2 attribs;

void main()
{
    payload.weights = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    payload.primitive = gl_PrimitiveID;
}
