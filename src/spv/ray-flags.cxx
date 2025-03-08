/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>

#include "../../external/spirv.hpp"
export module spv.rayFlags;

export class RayFlags {
    unsigned rayFlags;

public:
    RayFlags(unsigned flags): rayFlags(flags) {
        // The flags are defined by SPIR-V: spv::RayFlagsMask
        // Do some asserts to verify the value provided is valid:
        assert(!(opaque() && noOpaque()));
    }

    unsigned get() const {
        return rayFlags;
    }

    bool none() const {
        return (rayFlags | spv::RayFlagsMask::RayFlagsMaskNone) == 0;
    }
    bool opaque() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsOpaqueKHRMask;
    }
    bool noOpaque() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsNoOpaqueKHRMask;
    }
    bool terminateOnFirstHit() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsTerminateOnFirstHitKHRMask;
    }
    bool skipClosestHitShader() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsSkipClosestHitShaderKHRMask;
    }
    bool cullBackFacingTriangles() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsCullBackFacingTrianglesKHRMask;
    }
    bool cullFrontFacingTriangles() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsCullFrontFacingTrianglesKHRMask;
    }
    bool cullOpaque() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsCullOpaqueKHRMask;
    }
    bool cullNoOpaque() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsCullNoOpaqueKHRMask;
    }
    bool skipTriangles() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsSkipTrianglesKHRMask;
    }
    bool skipAABBs() const {
        return rayFlags & spv::RayFlagsMask::RayFlagsSkipAABBsKHRMask;  // skip procedurals
    }
};
