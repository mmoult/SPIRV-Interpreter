/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <vector>

#include "data/manager.h"
#include "../external/spirv.hpp"
#include "../values/value.hpp"
#include "../values/raytrace/trace.hpp"
#include "../values/raytrace/node.hpp"
export module spv.raySubstage;
import spv.data.data;
import spv.frame;
import spv.instruction;
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

public:
    // entry location in instructions vector
    unsigned entry = 0;
    // root/global for the substage
    DataView* data = nullptr;
    // index within data of the variable interface
    std::vector<unsigned> ins;
    std::vector<unsigned> outs;
    std::vector<unsigned> specs;
    // From the extra input file- must be used to refresh the data for each new execution
    ValueMap inputs;

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
        if (var.getStorageClass() == spv::StorageClassIncomingRayPayloadKHR) {
            payload = loc;
            return true;
        }
        return false;
    }

    void setUpInputs(AccelStruct& as, Value& payload) {
        DataView& dat = *data;
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
        const Intersection& cand = trace.getCandidate();
        const InstanceNode* instance = nullptr;
        if (cand.instance != nullptr)
            instance = cand.instance;
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
            Variable* var = dat[tpayload].getVariable();
            assert(var != nullptr);
            var->getVal()->copyFrom(payload);
        }
    }

    void cleanUp(Frame& frame) {
        DataView& dat = *data;
        // Save from the frame rt result into the payload (as necessary)
        if (frame.getRtTrigger() == RtStageKind::INTERSECTION) {
            if (payload != 0) {
                Variable* var = dat[payload].getVariable();
                assert(var != nullptr);
                frame.getRtResult()->copyFrom(*var->getVal());
            }
        }
    }
};
