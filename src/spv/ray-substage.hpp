/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_RAYSUBSTAGE_HPP
#define SPV_RAYSUBSTAGE_HPP

#include <cassert>
#include <vector>

#include "../util/spirv.hpp"
#include "../values/aggregate.hpp"
#include "../values/primitive.hpp"
#include "../values/raytrace/accel-struct.hpp"
#include "../values/raytrace/node.hpp"
#include "../values/raytrace/trace.hpp"
#include "../values/value.hpp"
#include "data/data.hpp"
#include "data/manager.hpp"
#include "frame.hpp"
#include "instruction.hpp"

struct RayTraceSubstage {
    // locations of some important variables
    std::vector<unsigned> worldRayOrigin;
    std::vector<unsigned> worldRayDirection;
    std::vector<unsigned> rayTMax;
    std::vector<unsigned> rayTMin;
    std::vector<unsigned> instanceCustomIndex;
    std::vector<unsigned> geomIndex;

    // The location of the output payload, which should be the only variable with storage class IncomingRayPayloadKHR.
    // Note: it is called "incoming" because the payload is input (and may be modified) before output.
    unsigned payload = 0;
    unsigned accelStruct = 0;
    // The spec says there can only be one hit attribute variable (if any). Only writable in intersection, but readable
    // in any-hit, closest-hit, intersection (although undefined before first write).
    unsigned hitAttribute = 0;

    // The acceleration struct from which this substage was generated. Used to set up rt fields in any recursive stage.
    AccelStruct* callingAs = nullptr;


    ValueMap getVariables(const std::vector<unsigned>& vars) const {
        ValueMap ret;
        for (const auto v : vars) {
            const auto var = (*data)[v].getVariable();
            ret.emplace(var->getName(), &var->getVal());
        }
        return ret;
    }

public:
    // entry location in instructions vector
    unsigned entry = 0;
    // root/global for the substage
    DataView* data = nullptr;
    // index within data of the variable interface
    std::vector<unsigned> ins;
    std::vector<unsigned> outs;
    std::vector<unsigned> specs;

    /// @brief handle the given instruction, which is in a static context
    /// @param inst the instruction to handle
    /// @return whether the instruction was adequately handled and should skip ioGen
    bool handleStaticInst(const Instruction& inst) {
        const Variable* var_p = (*data)[inst.getResult()].getVariable();
        if (var_p == nullptr)
            return false;
        const Variable& var = *var_p;
        unsigned loc = inst.getResult();

        switch (var.getBuiltIn()) {
        case spv::BuiltIn::BuiltInWorldRayOriginKHR:
            worldRayOrigin.push_back(loc);
            return true;
        case spv::BuiltIn::BuiltInWorldRayDirectionKHR:
            worldRayDirection.push_back(loc);
            return true;
        case spv::BuiltIn::BuiltInRayTmaxKHR:
            rayTMax.push_back(loc);
            return true;
        case spv::BuiltIn::BuiltInRayTminKHR:
            rayTMin.push_back(loc);
            return true;
        case spv::BuiltIn::BuiltInInstanceCustomIndexKHR:
            instanceCustomIndex.push_back(loc);
            return true;
        case spv::BuiltIn::BuiltInRayGeometryIndexKHR:
            geomIndex.push_back(loc);
            return true;
        default:
            break;
        }

        // Check the storage class and type to find payload and accel struct
        if (var.getVal().getType().getBase() == DataType::ACCEL_STRUCT) {
            accelStruct = loc;
            return true;
        }
        spv::StorageClass storage = var.getStorageClass();
        if (storage == spv::StorageClassIncomingRayPayloadKHR || storage == spv::StorageClassIncomingCallableDataNV) {
            payload = loc;
            return true;
        } else if (storage == spv::StorageClassHitAttributeKHR) {
            hitAttribute = loc;
            return true;
        }
        return false;
    }

    /// @brief Set up all inputs except the hit attribute, which takes some special processing
    void setUpInputs(DataView& dat, AccelStruct* as, Value& payload, const InstanceNode* instance) const;

    /// @brief Set up the hit attribute
    /// The set up will fall into one of these four cases:
    /// 1) may need to be generated to later reference (intersection)
    /// 2) may come from a previous stage (intersection -> ahit, rchit)
    /// 3) may need to be created from the intersection's barycentrics (triangle hit -> ahit, rchit)
    /// 4) may not be needed at all (possible for all stages)
    [[nodiscard]] Value*
    setUpHitAttribute(RtStageKind stage, DataView& dat, glm::vec2 barycentrics, Value* hit_attribute) const;

    /// @brief Get a map of the record input, used primarily for generating templates
    /// Although an overwhelming majority of values in the substage should be derived from a location or a builtin,
    /// there is an allowance to record-specific values. These values need to be passed per-substage.
    ValueMap getRecordInputs() const;

    void cleanUp(Frame& frame) const;
};
#endif
