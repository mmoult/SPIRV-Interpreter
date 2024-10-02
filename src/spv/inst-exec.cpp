/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <stack>
#include <string>
#include <variant>
#include <vector>

#define SPV_ENABLE_UTILITY_CODE 1
#include "../external/spirv.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "../values/raytrace/trace.hpp"
#include "data/manager.h"
module spv.instruction;
import spv.data.data;
import spv.frame;
import spv.token;
import util.ternary;
import value.aggregate;
import value.image;
import value.primitive;
import value.raytrace.accelStruct;
import value.raytrace.rayQuery;
import value.statics;

void thing() {
    /*//////////////////////////////////
    candidateIntersection.properties.instance = std::static_pointer_cast<InstanceNode>(*curr_node_ref);

    // Run the intersection shader if the intersection was with a procedural node.
    // Running the shader here because we need the instance SBT offset for calculating the index.
    if (useSBT) {
        intersectedProcedural = true;
        const int geometry_index = getIntersectionGeometryIndex(false);
        const unsigned instance_sbt_offset =
            getIntersectionInstanceShaderBindingTableRecordOffset(false);
        //
        const Program* shader = shaderBindingTable.getHitShader(
            offsetSBT,
            strideSBT,
            geometry_index,
            instance_sbt_offset,
            HitGroupType::Intersection
        );
        if (shader != nullptr) {
            ValueMap inputs = getNewShaderInputs(*shader, true, nullptr);
            SBTShaderOutput outputs = shaderBindingTable.executeHit(
                inputs,
                offsetSBT,
                strideSBT,
                geometry_index,
                instance_sbt_offset,
                HitGroupType::Intersection
            );

            // If it fails the intersection, then cancel it.
            // Note: variable <intersectedProcedural> will be modified when invoking intersection and
            // any-hit shaders.
            const bool missed = !intersectedProcedural;
            if (missed) {
                found = false;
                candidateIntersection.update(false, IntersectionProperties {});
                break;
            }

            // Otherwise, store the hit attribute for later use.
            for (const auto& [name, info] : outputs) {
                const auto value = get<0>(info);
                const auto storage_class = get<1>(info);
                if (storage_class == spv::StorageClass::StorageClassHitAttributeKHR) {
                    Value* hit_attribute = value->getType().construct();
                    hit_attribute->copyFrom(*value);
                    candidateIntersection.properties.hitAttribute = hit_attribute;
                }
            }
        }
    }
    *////////////////////////////
}

void invokeSubstageShader(Frame& frame, AccelStruct& as, Value* payload, RtStageKind kind) {
    const Trace& trace = as.getTrace();
    auto& candidate = trace.getCandidate();
    int geom_index = candidate.geometryIndex;
    unsigned instance_sbt_offset = 0;
    if (candidate.instance != nullptr)
        instance_sbt_offset = candidate.instance->getSbtRecordOffs();
    // index = instance_sbt_offset + sbt_offset + (geometry_index * sbt_stride)
    unsigned index = instance_sbt_offset + trace.offsetSBT + (geom_index * trace.strideSBT);
    frame.triggerRaytrace(kind, index, payload, as);
}

bool Instruction::execute(DataView& data, std::vector<Frame*>& frame_stack, bool verbose) const {
    bool inc_pc = true;
    bool blocked = false;
    Frame& frame = *frame_stack.back();

    unsigned result_at;
    if (hasResult) {
        unsigned idx = hasResultType? 1: 0;
        assert(operands[idx].type == Token::Type::REF);
        result_at = std::get<unsigned>(operands[idx].raw);
    }

    // Pops the current frame and returns whether there are any more frames after
    auto pop_frame = [&frame_stack]() {
        // If the frame before is a raytracing launch, don't delete the frame's data
        bool pop_to_rt = false;
        if (frame_stack.size() > 1) {
            const Frame* prev = frame_stack[frame_stack.size() - 2];
            if (prev->getRtTrigger() != RtStageKind::NONE) {
                frame_stack.back()->removeData();
                pop_to_rt = true;
            }
        }
        delete frame_stack.back();
        frame_stack.pop_back();
        return !(pop_to_rt || frame_stack.empty());
    };

    Value* dst_val = nullptr;
    switch (opcode) {
    default:
        // fall back on the makeResult function (no fallback should use location!)
        if (!makeResult(data, 0, nullptr)) {
            std::stringstream err;
            err << "Unsupported instruction execution (" << spv::OpToString(opcode) << ")!";
            throw std::runtime_error(err.str());
        }
        // If the instruction did make a result, success! These instructions cannot modify control flow,
        // so assume inc_pc = true
        break;
    case spv::OpNop: // 1
    case spv::OpLine: // 8
    case spv::OpNoLine: // 317
    case spv::OpModuleProcessed: // 330
        // No semantic value. Kept only for predictability / debugging. Do nothing
        break;
    case spv::OpFunction: // 54
    case spv::OpMemoryBarrier: // 225
    case spv::OpLoopMerge: // 246
    case spv::OpSelectionMerge: // 247
        break;  // should print for verbose
    case spv::OpFunctionParameter: { // 55
        inc_pc = false;  // get arg increments PC for us
        spv::StorageClass storage = spv::StorageClass::StorageClassFunction;
        Type* var_type = getType(0, data);
        Variable* var = Variable::makeVariable(storage, *var_type);
        const Value* arg = frame.getArg();
        // No need to clone the arg since we only delete from the data, not the arg list
        var->setVal(*arg);
        data[result_at].redefine(var);
        break;
    }
    case spv::OpFunctionEnd: // 56
        throw std::runtime_error("Missing return before function end!");
    case spv::OpFunctionCall: { // 57
        // Note: cannot call an entry point, right?
        Function* fx = getFunction(2, data);
        std::vector<const Value*> args;
        for (unsigned i = 3; i < operands.size(); ++i) {
            const Variable* var = getVariable(i, data);
            if (var == nullptr) {
                std::stringstream err;
                err << "Each argument to OpFunctionCall must be a variable! Operand " << (i - 3) << " is not.";
                throw std::runtime_error(err.str());
            }
            args.push_back(var->getVal());
        }

        // If the result has void type, pass in 0 instead of result_at
        const Type* return_type = getType(0, data);
        if (return_type != nullptr && return_type->getBase() == DataType::VOID)
            result_at = 0;

        frame_stack.push_back(new Frame(fx->getLocation(), args, result_at, data));
        inc_pc = false;
        break;
    }
    case spv::OpVariable: // 59
        // This instruction has been run before (during the static pass), so we can assume here the variable already
        // exists. Now, all we need to do is set the default value (in case not set before)
        if (operands.size() > 3) { // included default value
            Variable* var = getVariable(1, data);
            Value* defaultVal = getValue(3, data);
            var->setVal(*defaultVal);
        }
        break;
    case spv::OpLoad: { // 61
        Type* ret_type = getType(0, data);
        Value* from_val = getFromPointer(2, data);

        // The SPIR-V spec handles images differently.
        if (ret_type->getBase() == DataType::IMAGE) {
            // Unlike aggregates which own their data, images have metadata and a non-owning reference to the texels.
            // Due to this, each load from a variable will share the same texels. Since the metadata is constant, we can
            // simulate this by reusing the same image object
            Data weak = Data::weak(from_val);
            data[result_at].redefine(weak);
        } else {
            // Construct a new value to serve as result, then copy the result val to it
            dst_val = ret_type->construct();
            // Load from a pointer, which may be a variable
            dst_val->copyFrom(*from_val);
        }
        break;
    }
    case spv::OpStore: { // 62
        Value* val = getValue(1, data);
        Value* store_to = getFromPointer(0, data);
        store_to->copyFrom(*val);
        break;
    }
    case spv::OpImageWrite: { // 99
        Value* image_v = getValue(0, data);
        if (image_v->getType().getBase() != DataType::IMAGE)
            throw std::runtime_error("The third operand to ImageRead must be an image!");
        auto& image = static_cast<Image&>(*image_v);
        const Value* coords_v = getValue(1, data);
        // coords can be a scalar or vector of int or float type
        const Type* coord_type = &coords_v->getType();
        bool arrayed = false;
        if (coord_type->getBase() == DataType::ARRAY) {
            coord_type = &coord_type->getElement();
            arrayed = true;
        }
        const Value* texel = getValue(2, data);
        // If the texel is a single value, we need to compose it in a temporary array
        const Array* composed;
        if (texel->getType().getBase() == DataType::ARRAY)
            composed = static_cast<const Array*>(texel);
        else {
            // TODO HERE
            throw std::runtime_error("Unimplemented ImageWrite variant!");
        }

        DataType base = coord_type->getBase();
        if (base == DataType::INT) {
            auto [x, y, z] = Image::extractIntCoords(arrayed, coords_v);
            image.write(x, y, z, *composed);
        } else { // if (base == DataType::FLOAT) {
            // TODO
            throw std::runtime_error("Float coordinates to image read not supported yet!");
        }
        break;
    }
    case spv::OpControlBarrier: { // 224
        blocked = true;
        // TODO surely there is more to do here...
        break;
    }
    case spv::OpPhi: { // 245
        unsigned last_label = frame.getLabel();
        // We must find a label in the phi which matches the last block seen
        for (unsigned i = 3; i < operands.size(); i += 2) {
            Value* block = getValue(i, data);
            auto p_block = static_cast<Primitive*>(block);
            if (p_block->data.u32 == last_label) {
                dst_val = getValue(i - 1, data);
                break;
            }
        }
        if (dst_val == nullptr)
            throw std::runtime_error("Phi encountered without a label for the last block!");

        // Need to clone the destination value for data safety
        Value* real_dst = dst_val->getType().construct();
        real_dst->copyFrom(*dst_val);
        dst_val = real_dst;
        break;
    }
    case spv::OpLabel: { // 248
        Value* val = getValue(0, data);  // get the label value which has been made earlier
        auto prim = static_cast<Primitive*>(val);
        frame.setLabel(prim->data.u32);
        break;
    }
    case spv::OpBranch: { // 249
        Value* dstv = getValue(0, data);
        Primitive* dst = static_cast<Primitive*>(dstv);
        frame.setPC(dst->data.u32);
        inc_pc = false;
        break;
    }
    case spv::OpBranchConditional: { // 250
        Value* condv = getValue(0, data);
        Primitive* cond = static_cast<Primitive*>(condv);
        Value* branchv = getValue((cond->data.b32)? 1 : 2, data);
        Primitive* branch = static_cast<Primitive*>(branchv);
        frame.setPC(branch->data.u32);
        inc_pc = false;
        break;
    }
    case spv::OpKill: // 252
    case spv::OpTerminateInvocation: { // 4416
        // Completely stops execution
        while (pop_frame())
            ;
        inc_pc = false;
        break;
    }
    case spv::OpIgnoreIntersectionKHR: // 4448
    case spv::OpTerminateRayKHR: { // 4449
        /*
        assert(extra_data != nullptr);
        bool* ignore_intersection = static_cast<bool*>(extra_data);
        *ignore_intersection = (opcode == spv::OpIgnoreIntersectionKHR);
        */
        [[fallthrough]];
    }
    case spv::OpReturn: // 253
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        inc_pc = pop_frame(); // don't increment PC if we are at the end of program
        break;
    case spv::OpReturnValue: { // 254
        if (!frame.hasReturn())
            throw std::runtime_error("Void function tried to return a value!");
        Value* val = getValue(0, data);
        // For correctness, we must clone. Consider the case where the return of some function is passed as an argument
        // to another call of the same function. The return could be (re)defined before the argument is used.
        Value* ret = val->getType().construct();
        ret->copyFrom(*val);
        auto ret_at = frame.getReturn();
        inc_pc = pop_frame();
        // Save the return onto the previous frame
        frame_stack.back()->getData()[ret_at].redefine(ret);
        break;
    }
    case spv::OpTraceRayKHR: { // 4445
        AccelStruct& as = static_cast<AccelStruct&>(*getValue(0, data));

        // We use the trigger to keep track of rt stage:
        // 1) NONE: first appearance at the instruction
        // 2) INTERSECTION: returned after intersection (which itself may have invoked any hit)
        // 3) CLOSEST or MISS: return after processing the chosen hit/miss
        auto prev_stage = frame.getRtTrigger();
        if (prev_stage != RtStageKind::MISS && prev_stage != RtStageKind::CLOSEST) {
            if (prev_stage == RtStageKind::NONE) {
                const unsigned ray_flags = static_cast<Primitive&>(*getValue(1, data)).data.u32;
                const unsigned cull_mask = static_cast<Primitive&>(*getValue(2, data)).data.u32;
                const unsigned offset_sbt = static_cast<Primitive&>(*getValue(3, data)).data.u32;
                const unsigned stride_sbt = static_cast<Primitive&>(*getValue(4, data)).data.u32;
                const unsigned miss_index = static_cast<Primitive&>(*getValue(5, data)).data.u32;

                std::vector<float> ray_origin = Statics::extractVec(getValue(6, data), "ray_origin", 3);
                std::vector<float> ray_direction = Statics::extractVec(getValue(8, data), "ray_direction", 3);

                const float ray_t_min = static_cast<Primitive&>(*getValue(7, data)).data.fp32;
                const float ray_t_max = static_cast<Primitive&>(*getValue(9, data)).data.fp32;

                // Run it through our implementation of a ray tracing pipeline
                as.initTrace(
                    ray_flags,
                    cull_mask & 0xFF,  // Only the 8 least-significant bits of Cull Mask are used
                    ray_origin,
                    ray_direction,
                    ray_t_min,
                    ray_t_max,
                    true,
                    offset_sbt & 0xF,    // Only the 4 least-significant bits of SBT Offset are used
                    stride_sbt & 0xF,    // Only the 4 least-significant bits of SBT Stride are used
                    miss_index & 0xFFFF  // Only the 16 least-significant bits of Miss Index are used
                );
            } else {
                // handle the result of the intersection shader

            }

            // Return whether at least one intersection was made
            Ternary status = as.traceRay(frame.getRtTrigger() != RtStageKind::NONE);
            if (status == Ternary::MAYBE) {
                // We need to launch a substage here
                invokeSubstageShader(frame, as, new Primitive(false), RtStageKind::INTERSECTION);
                inc_pc = false;
                break;
            }

            // Payload should either be filled with whether the trace intersected a geometry (a boolean)
            // or the user-defined payload output.
            Variable* payload_var = getVariable(10, data);
            Value* payload = payload_var->getVal();

            // Do not invoke any shaders if a shader binding table was not specified
            bool used_sbt = false;
            const Trace& trace = as.getTrace();
            if (trace.useSBT) {
                // Otherwise, invoke either the closest hit or miss shaders
                if (trace.hasCommitted()) {
                    // Closest hit
                    if (!trace.rayFlags.skipClosestHitShader()) {
                        invokeSubstageShader(frame, as, payload, RtStageKind::CLOSEST);
                        used_sbt = true;
                    }
                } else {
                    // Miss
                    invokeSubstageShader(frame, as, payload, RtStageKind::MISS);
                    used_sbt = true;
                }
            }

            // If the expected shader was missing from the SBT or if we shouldn't use the SBT, fill in default
            if (!used_sbt) {
                bool intersect_once = status == Ternary::YES;
                std::stack<Value*> frontier;
                frontier.push(payload);

                while (!frontier.empty()) {
                    Value* curr = frontier.top();
                    frontier.pop();

                    switch (curr->getType().getBase()) {
                    default: {
                        std::stringstream err;
                        err << "Cannot fill data of unsupported payload type: " << curr->getType().getBase();
                        throw std::runtime_error(err.str());
                    }
                    case DataType::FLOAT:
                    case DataType::UINT:
                    case DataType::INT: {
                        Primitive& val = static_cast<Primitive&>(*curr);
                        val.copyFrom(Primitive(intersect_once? 1: 0));
                        break;
                    }
                    case DataType::BOOL: {
                        Primitive& val = static_cast<Primitive&>(*curr);
                        val.copyFrom(Primitive(intersect_once));
                        break;
                    }
                    case DataType::ARRAY:
                    case DataType::STRUCT: {
                        for (auto it : static_cast<const Aggregate&>(*curr))
                            frontier.push(it);
                        break;
                    }
                    }
                }
            } else {
                inc_pc = false;
                break;  // break to launch next substage
            }
        } else {
            // Handle the result of the closest or miss shader
            // Payload should already be copied into the substage's result, and since we loaded it as a reference to the
            // variable's value, the copy should have done our work for us.
        }
        frame.disableRaytrace();
        break;
    }
    case spv::OpExecuteCallableKHR: { // 4446
        // TODO: implement callable shader execution
        const unsigned index_sbt = static_cast<Primitive&>(*getValue(0, data)).data.u32;
        const auto shader_args = getFromPointer(1, data);
        std::cout << "WARNING: OpExecuteCallableKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "Invoking callable shader at SBT index = (" << index_sbt << ") with argument of type ("
                  << shader_args->getType().getBase() << ")" << std::endl;
        break;
    }
    case spv::OpRayQueryInitializeKHR: { // 4473
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        AccelStruct& as = static_cast<AccelStruct&>(*getValue(1, data));
        const unsigned ray_flags = static_cast<Primitive&>(*getValue(2, data)).data.u32;
        const unsigned cull_mask = static_cast<Primitive&>(*getValue(3, data)).data.u32;
        std::vector<float> ray_origin = Statics::extractVec(getValue(4, data), "ray_origin", 3);
        const float ray_t_min = static_cast<Primitive&>(*getValue(5, data)).data.fp32;
        std::vector<float> ray_direction = Statics::extractVec(getValue(6, data), "ray_direction", 3);
        const float ray_t_max = static_cast<Primitive&>(*getValue(7, data)).data.fp32;

        ray_query.setAccelStruct(as);
        ray_query.getAccelStruct().initTrace(
            ray_flags, cull_mask & 0xFF, ray_origin, ray_direction, ray_t_min, ray_t_max, false
        );
        break;
    }
    case spv::OpRayQueryTerminateKHR: { // 4474
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        ray_query.getAccelStruct().terminate();
        break;
    }
    case spv::OpRayQueryGenerateIntersectionKHR: { // 4475
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        const float t_hit = static_cast<Primitive&>(*getValue(1, data)).data.fp32;
        ray_query.getAccelStruct().generateIntersection(t_hit);
        break;
    }
    case spv::OpRayQueryConfirmIntersectionKHR: { // 4476
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        ray_query.getAccelStruct().confirmIntersection();
        break;
    }
    case spv::OpRayQueryProceedKHR: { // 4477
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        AccelStruct& as = ray_query.getAccelStruct();

        Ternary status = Ternary::NO;
        if (frame.getRtTrigger() == RtStageKind::NONE) {
            status = as.stepTrace();
        } else {
            // Handle the result of the previous stage (should only ever be intersection)
            // status =
            frame.disableRaytrace();
        }

        if (status == Ternary::MAYBE) {
            invokeSubstageShader(frame, as, new Primitive(false), RtStageKind::INTERSECTION);
            inc_pc = false;
            break;
        }

        if (status == Ternary::YES && as.getTrace().rayFlags.terminateOnFirstHit())
            as.terminate();

        dst_val = new Primitive(status == Ternary::YES);
        break;
    }
    case spv::OpReportIntersectionKHR: { // 5334
        const float t_hit = static_cast<Primitive&>(*getValue(2, data)).data.fp32;
        const unsigned hit_kind = static_cast<Primitive&>(*getValue(3, data)).data.u32;

        // Get necessary data from the ray tracing pipeline if it exist
        AccelStruct* accel_struct = nullptr;
        //if (extra_data != nullptr)
        //    accel_struct = static_cast<AccelStruct*>(extra_data);

        bool result = false;
        if (accel_struct == nullptr) {
            // Execute this if testing a single intersection shader: Assume range is [0.0, infinity)
            result = t_hit > 0.0f;
        } else {
            // Execute if running a ray tracing pipeline
            result = accel_struct->isIntersectionValid(t_hit);
            // TODO invoke any hit shader here
            //if (result)
            //    result = accel_struct->invokeAnyHitShader(t_hit, hit_kind);
        }

        dst_val = getType(0, data)->construct();
        Primitive prim(result);
        dst_val->copyFrom(prim);
        break;
    }
    }

    if (dst_val != nullptr) {
        assert(hasResult);
        data[result_at].redefine(dst_val);
    }

    if (inc_pc)
        frame_stack.back()->incPC();

    return blocked;
}

void Instruction::print() const {
    std::cout << spv::OpToString(opcode);

    unsigned i = 0;
    if (hasResultType) {
        std::cout << " ";
        operands[i].print();
        ++i;
    }
    if (hasResult) {
        std::cout << " ";
        operands[i].print();
        ++i;
    }

    if (hasResultType || hasResult)
        std::cout << " =";

    for (; i < operands.size(); ++i) {
        std::cout << " ";
        operands[i].print();
    }
    std::cout << std::endl;
}
