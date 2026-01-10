/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "ray-substage.hpp"

void copy_into(Value* into, std::vector<Primitive>& src) {
    Array& into_arr = static_cast<Array&>(*into);
    for (unsigned i = 0; i < src.size(); ++i)
        into_arr[i]->copyFrom(src[i]);
}

void RayTraceSubstage::setUpInputs(DataView& dat, AccelStruct* as, Value& payload, const InstanceNode* instance) const {
    #define REQUIRE_AS(VAR_TYPE) \
        if (as == nullptr) { \
            throw std::runtime_error("Cannot set up " VAR_TYPE " variable without an acceleration struct in the frame!"); \
        }

    if (!worldRayOrigin.empty()) {
        REQUIRE_AS("world ray origin");
        std::vector<Primitive> origin = as->getWorldRayOrigin();
        for (unsigned loc : worldRayOrigin) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            copy_into(&var->getVal(), origin);
        }
    }
    if (!worldRayDirection.empty()) {
        REQUIRE_AS("world ray direction");
        std::vector<Primitive> direction = as->getWorldRayDirection();
        for (unsigned loc : worldRayDirection) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            copy_into(&var->getVal(), direction);
        }
    }
    if (!rayTMax.empty()) {
        REQUIRE_AS("ray t maximum");
        const Trace& trace = as->getTrace();
        Primitive tmax(trace.rayTMax);
        for (unsigned loc : rayTMax) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            var->getVal().copyFrom(tmax);
        }
    }
    if (!rayTMin.empty()) {
        REQUIRE_AS("ray t minimum");
        const Trace& trace = as->getTrace();
        Primitive tmin(trace.rayTMin);
        for (unsigned loc : rayTMin) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            var->getVal().copyFrom(tmin);
        }
    }
    if (!geomIndex.empty()) {
        REQUIRE_AS("geometry index");
        const Trace& trace = as->getTrace();
        const Intersection& sect = trace.hasCommitted() ? trace.getCommitted() : trace.getCandidate();
        Primitive geom_index(static_cast<unsigned>(sect.geometryIndex));
        for (unsigned loc : geomIndex) {
            Variable* var = dat[loc].getVariable();
            assert(var != nullptr);
            var->getVal().copyFrom(geom_index);
        }
    }
    Primitive custom_idx((instance == nullptr) ? 0 : instance->getCustomIndex());
    for (unsigned loc : instanceCustomIndex) {
        Variable* var = dat[loc].getVariable();
        assert(var != nullptr);
        var->getVal().copyFrom(custom_idx);
    }

    if (accelStruct != 0) {
        REQUIRE_AS("acceleration struct");
        Variable* var = dat[accelStruct].getVariable();
        assert(var != nullptr);
        var->getVal().copyFrom(*as);
    }
    if (unsigned tpayload = this->payload; tpayload != 0) {
        try {
            Variable* var = dat[tpayload].getVariable();
            assert(var != nullptr);
            var->getVal().copyFrom(payload);
        } catch (const std::runtime_error& _) {
            throw std::runtime_error("Cannot invoke raytracing substage with incorrect payload type!");
        }
    }
}

[[nodiscard]] Value*
RayTraceSubstage::setUpHitAttribute(
    RtStageKind stage, DataView& dat, glm::vec2 barycentrics, Value* hit_attribute
) const {
    if (hitAttribute != 0) {
        Variable* var = dat[hitAttribute].getVariable();
        Value& hit_attrib_val = var->getVal();
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

ValueMap RayTraceSubstage::getRecordInputs() const {
    ValueMap input_map;
    for (unsigned v : ins) {
        const auto& var = *(*data)[v].getVariable();
        if (var.getStorageClass() == spv::StorageClassShaderRecordBufferKHR)
            input_map.emplace(var.getName(), &var.getVal());
    }
    auto spec_consts = getVariables(specs);
    input_map.insert(spec_consts.begin(), spec_consts.end());
    return input_map;
}

void RayTraceSubstage::cleanUp(Frame& frame) const {
    DataView& dat = *frame.getRtData();
    // Save from the frame rt result into the payload (as necessary)
    auto stage = frame.getRtTrigger();
    if (payload != 0) {
        Variable* var = dat[payload].getVariable();
        assert(var != nullptr);
        frame.getRtResult()->copyFrom(var->getVal());
    }
    // Save updates to the hit attribute (if present)
    if (stage == RtStageKind::INTERSECTION && hitAttribute != 0) {
        Variable* var = dat[hitAttribute].getVariable();
        assert(var != nullptr);
        frame.getHitAttribute()->copyFrom(var->getVal());
    }
}
