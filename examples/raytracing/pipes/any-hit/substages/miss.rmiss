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

void main()
{
    payload.weights = vec3(0.0, 0.0, 0.2);
}
