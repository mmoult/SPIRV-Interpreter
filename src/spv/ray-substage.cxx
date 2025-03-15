/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <vector>

#include "data/manager.h"
#include "../../external/spirv.hpp"
#include "../values/value.hpp"
#include "../values/raytrace/trace.hpp"
#include "../values/raytrace/node.hpp"
export module spv.raySubstage;
import spv.data.data;
import spv.frame;
import spv.instruction;
import value.aggregate;
import value.primitive;
import value.raytrace.accelStruct;

void copy_into(Value* into, std::vector<Primitive>& src) {
    Array& into_arr = static_cast<Array&>(*into);
    for (unsigned i = 0; i < src.size(); ++i)
        into_arr[i]->copyFrom(src[i]);
}

export struct RayTraceSubstage {
    // locations of some important variables
    std::vector<unsigned> worldRayOrigin;
    std::vector<unsigned> worldRayDirection;
    std::vector<unsigned> rayTMax;
    std::vector<unsigned> rayTMin;
    std::vector<unsigned> instanceCustomIndex;

    // The location of the output payload, which should be the only variable with storage class IncomingRayPayloadKHR.
    // Note: it is called "incoming" because the payload is input (and may be modified) before output.
    unsigned payload = 0;
    unsigned accelStruct = 0;
    // The spec says there can only be one hit attribute variable (if any). Only writable in intersection, but readable
    // in any-hit, closest-hit, intersection (although undefined before first write).
    unsigned hitAttribute = 0;

    ValueMap getVariables(const std::vector<unsigned>& vars) const {
        ValueMap ret;
        for (const auto v : vars) {
            const auto var = (*data)[v].getVariable();
            ret.emplace(var->getName(), var->getVal());
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
        default:
            break;
        }

        // Check the storage class and type to find payload and accel struct
        if (var.getVal()->getType().getBase() == DataType::ACCEL_STRUCT) {
            accelStruct = loc;
            return true;
        }
        spv::StorageClass storage = var.getStorageClass();
        if (storage == spv::StorageClassIncomingRayPayloadKHR) {
            payload = loc;
            return true;
        } else if (storage == spv::StorageClassHitAttributeKHR) {
            hitAttribute = loc;
            return true;
        }
        return false;
    }

    /// @brief Set up all inputs except the hit attribute, which takes some special processing
    void setUpInputs(DataView& dat, AccelStruct& as, Value& payload, const InstanceNode* instance) const {
        std::vector<Primitive> origin = as.getWorldRayOrigin();
        for (unsigned loc : worldRayOrigin) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            copy_into(var->getVal(), origin);
        }
        std::vector<Primitive> direction = as.getWorldRayDirection();
        for (unsigned loc : worldRayDirection) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            copy_into(var->getVal(), direction);
        }
        const Trace& trace = as.getTrace();
        Primitive tmax(trace.rayTMax);
        for (unsigned loc : rayTMax) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            var->getVal()->copyFrom(tmax);
        }
        Primitive tmin(trace.rayTMin);
        for (unsigned loc : rayTMin) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            var->getVal()->copyFrom(tmin);
        }
        Primitive customIdx((instance == nullptr)? 0 : instance->getCustomIndex());
        for (unsigned loc : instanceCustomIndex) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            var->getVal()->copyFrom(customIdx);
        }

        if (accelStruct != 0) {
            Variable* var = dat[accelStruct].getVariable();
            assert(var != nullptr);
            var->getVal()->copyFrom(as);
        }
        if (unsigned tpayload = this->payload; tpayload != 0) {
            try {
                Variable* var = dat[tpayload].getVariable();
                assert(var != nullptr);
                var->getVal()->copyFrom(payload);
            } catch (const std::runtime_error& _) {
                throw std::runtime_error("Cannot invoke raytracing substage with incorrect payload type!");
            }
        }
    }

    /// @brief Set up the hit attribute
    /// The set up will fall into one of these four cases:
    /// 1) may need to be generated to later reference (intersection)
    /// 2) may come from a previous stage (intersection -> ahit, rchit)
    /// 3) may need to be created from the intersection's barycentrics (triangle hit -> ahit, rchit)
    /// 4) may not be needed at all (possible for all stages)
    [[nodiscard]] Value* setUpHitAttribute(
        RtStageKind stage,
        DataView& dat,
        glm::vec2 barycentrics,
        Value* hit_attribute
    ) const {
        if (hitAttribute != 0) {
            Variable* var = dat[hitAttribute].getVariable();
            Value& hit_attrib_val = *var->getVal();
            if (hit_attribute == nullptr) {
                if (stage == RtStageKind::INTERSECTION) {
                    // For intersection case, create the hit attribute
                    assert(var != nullptr);
                    return hit_attrib_val.getType().construct();
                } else {
                    // Try to create a hit attribute from the barycentrics
                    if (hit_attrib_val.getType().getBase() == DataType::ARRAY) {
                        Array& arr = static_cast<Array&>(hit_attrib_val);
                        if (unsigned arr_size = arr.getSize(); arr_size == 2 || arr_size == 3) {
                            // The barycentrics size is expected to be 2, but using 3 is a common mistake we will accept
                            for (unsigned i = 0; i < 2; ++i) {
                                Primitive prim(barycentrics[i]);
                                arr[i]->copyFrom(prim);
                            }
                            return nullptr;
                        }
                        // If the array length doesn't match expected, then it probably isn't intended as barycentric
                    }
                    throw std::runtime_error("Raytracing Substage launch missing non-barycentric hit attribute!");
                }
            } else {
                try {
                    assert(var != nullptr);
                    hit_attrib_val.copyFrom(*hit_attribute);
                } catch (const std::runtime_error& _) {
                    throw std::runtime_error("Cannot invoke raytracing substage with incorrect hit attribute type!");
                }
            }
        }
        return nullptr;
    }

    void cleanUp(Frame& frame) const {
        DataView& dat = *frame.getRtData();
        // Save from the frame rt result into the payload (as necessary)
        auto stage = frame.getRtTrigger();
        if ((stage == RtStageKind::CLOSEST || stage == RtStageKind::MISS) && payload != 0) {
            Variable* var = dat[payload].getVariable();
            assert(var != nullptr);
            frame.getRtResult()->copyFrom(*var->getVal());
        }
        // Save updates to the hit attribute (if present)
        if (stage == RtStageKind::INTERSECTION && hitAttribute != 0) {
            Variable* var = dat[hitAttribute].getVariable();
            assert(var != nullptr);
            frame.getHitAttribute()->copyFrom(*var->getVal());
        }
    }

    ValueMap getInputs() const {
        auto input_map = getVariables(ins);
        auto spec_consts = getVariables(specs);
        input_map.insert(spec_consts.begin(), spec_consts.end());
        return input_map;
    }
};
