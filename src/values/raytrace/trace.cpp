/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "trace.hpp"

glm::vec3 Intersection::getRayPos(const Trace* trace) const {
    glm::vec4 pos = worldToObj * glm::vec4(trace->rayOrigin.x, trace->rayOrigin.y, trace->rayOrigin.z, 1.0);
    return glm::vec3(pos.x, pos.y, pos.z);
}

glm::vec3 Intersection::getRayDir(const Trace* trace) const {
    glm::vec4 dir = worldToObj * glm::vec4(trace->rayDirection.x, trace->rayDirection.y, trace->rayDirection.z, 0.0);
    return glm::vec3(dir.x, dir.y, dir.z);
}
