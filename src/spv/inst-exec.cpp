/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#define SPV_ENABLE_UTILITY_CODE 1
#include "../../external/spirv.hpp"
#include "../values/raytrace/trace.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "data/manager.hpp"
module spv.instruction;
import front.console;
import spv.data.data;
import spv.frame;
import spv.token;
import util.ternary;
import value.aggregate;
import value.coopMatrix;
import value.image;
import value.primitive;
import value.raytrace.accelStruct;
import value.raytrace.rayQuery;
import value.statics;

void
invoke_substage_shader(RtStageKind kind, Frame& frame, AccelStruct& as, Value* hit_attrib, Value* payload = nullptr) {
    const Trace& trace = as.getTrace();
    int geom_index = 0;
    unsigned instance_sbt_offset = 0;
    if (kind != RtStageKind::MISS) {
        const auto& candidate = kind == RtStageKind::CLOSEST ? trace.getCommitted() : trace.getCandidate();
        geom_index = candidate.geometryIndex;
        if (candidate.instance != nullptr)
            instance_sbt_offset = candidate.instance->getSbtRecordOffs();
    } else
        instance_sbt_offset = trace.missIndex;

    unsigned index = instance_sbt_offset + trace.offsetSBT + (geom_index * trace.strideSBT);
    switch (kind) {
    default:
        throw std::runtime_error("Cannot invoke unsupported raytracing stage!");
        break;
    case RtStageKind::CLOSEST:
    case RtStageKind::MISS:
        assert(payload != nullptr);
        break;
    case RtStageKind::INTERSECTION:
        assert(payload == nullptr);
        // Payload signals whether the hit is accepted as a valid intersection
        payload = new Primitive(false);
        break;
    case RtStageKind::ANY_HIT: {
        assert(payload == nullptr);
        // The interface for an any hit shader is a little different, since it needs to report up:
        // 1) whether the hit is a valid intersection: defaults to true, false if IgnoreHit used.
        // 2) whether we should continue the search (the alternative being immediate exit from the intersect):
        //    defaults to true, false if AcceptHitAndEndSearch used.
        // We use bool[2] to represent this data
        std::vector<Value*> payload_el {new Primitive(true), new Primitive(true)};
        payload = new Array(payload_el);
        break;
    }
    }
    frame.triggerRaytrace(kind, index, payload, hit_attrib, as);
}

Frame* get_launching_frame(std::vector<Frame*>& frame_stack, RtStageKind expected) {
    unsigned launch_at = frame_stack.size() - 1;  // can skip current frame since it couldn't launch itself
    while (launch_at-- > 0) {
        if (auto trigger = frame_stack[launch_at]->getRtTrigger(); trigger != RtStageKind::NONE) {
            if (trigger != expected)
                throw std::runtime_error("Corrupted raytrace stack! Launching frame found of incorrect type!");
            return frame_stack[launch_at];
            break;
        }
    }
    return nullptr;
}

bool Instruction::execute(
    DataView& data,
    // The instruction is strictly forbidden from modifying any but the current frame stack
    std::vector<std::vector<Frame*>>& frame_stacks,
    unsigned invocation,
    unsigned num_invocations,
    bool verbose,
    bool use_sbt
) const {
    bool inc_pc = true;
    bool blocked = false;
    std::vector<Frame*>& frame_stack = frame_stacks[invocation];
    Frame& frame = *frame_stack.back();

    unsigned result_at;
    if (hasResult) {
        unsigned idx = hasResultType ? 1 : 0;
        assert(operands[idx].type == Token::Type::REF);
        result_at = std::get<unsigned>(operands[idx].raw);
    }

    // Pops the current frame and returns whether we should increment the PC
    auto pop_frame = [&frame_stack]() {
        bool pop_to_rt = false;
        if (frame_stack.size() > 1) {
            Frame* prev = frame_stack[frame_stack.size() - 2];
            auto stage = prev->getRtTrigger();
            pop_to_rt = stage != RtStageKind::NONE;

            if (stage == RtStageKind::CALLABLE)
                // Since we will be returning to the previous frame, mark it as returning
                // Callable requires this extra processing since it allows direct recursion
                prev->prepareReturn();
            // If the frame before is a raytracing launch, don't delete the frame's data
            else if (stage != RtStageKind::NONE)
                frame_stack.back()->removeData();
        }
        delete frame_stack.back();
        frame_stack.pop_back();
        return !(pop_to_rt || frame_stack.empty());
    };
    auto terminate_invocation = [&]() {
        while (pop_frame())
            ;
        inc_pc = false;
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
    case spv::OpNop:  // 1
    case spv::OpLine:  // 8
    case spv::OpNoLine:  // 317
    case spv::OpModuleProcessed:  // 330
        // No semantic value. Kept only for predictability / debugging. Do nothing
        break;
    case spv::OpFunction:  // 54
    case spv::OpMemoryBarrier:  // 225
    case spv::OpLoopMerge:  // 246
    case spv::OpSelectionMerge:  // 247
        break;  // should print for verbose
    case spv::OpFunctionParameter:  // 55
        inc_pc = false;  // get arg increments PC for us
        // Function parameters get a weak copy of the data passed in. If a pointer is passed (such as a Variable), then
        // changes to the pointed data in-function should remain after the function exits.
        data[result_at].redefine(frame.getArg(), false);
        break;
    case spv::OpFunctionEnd:  // 56
        throw std::runtime_error("Missing return before function end!");
    case spv::OpFunctionCall: {  // 57
        // Note: cannot call an entry point, right?
        Function* fx = getFunction(2, data);
        std::vector<Data*> args;
        for (unsigned i = 3; i < operands.size(); ++i)
            args.push_back(&getData(i, data));

        // If the result has void type, pass in 0 instead of result_at
        const Type* return_type = getType(0, data);
        if (return_type != nullptr && return_type->getBase() == DataType::VOID)
            result_at = 0;

        frame_stack.push_back(new Frame(fx->getLocation(), args, result_at, data));
        inc_pc = false;
        break;
    }
    case spv::OpVariable: {  // 59
        // This instruction has been run before (during the static pass), so we can assume here the variable already
        // exists at the global level.
        // If the storage class requires it, we must transfer the variable down to the local scope. Also, we must set
        // the default value (if any).
        Variable* var = data[result_at].getVariable();
        if (var->isThreaded()) {
            // Create the variable for the local scope
            var = new Variable(*var);
            data.local(result_at).redefine(var);
        }
        var->initValue(*getType(0, data));
        selectName(*var);
        auto fix_size = [&](Value& seen) {
            if (seen.getType().getBase() == DataType::COOP_MATRIX)
                static_cast<CoopMatrix&>(seen).enforceSize(invocation, num_invocations);
            return true;
        };
        var->getVal().recursiveApply(fix_size);
        if (operands.size() > 3) {  // included default value
            Value* defaultVal = getValue(3, data);
            var->getVal().copyFrom(*defaultVal);
        }
        break;
    }
    case spv::OpLoad: {  // 61
        Type* ret_type = getType(0, data);
        Value* from_val = getFromPointer(2, data);

        // The SPIR-V spec handles images differently.
        if (auto base = ret_type->getBase(); base == DataType::IMAGE || base == DataType::SAMPLED_IMG) {
            // Unlike aggregates which own their data, images have metadata and a non-owning reference to the texels.
            // Due to this, each load from a variable will share the same texels. Since the metadata is constant, we can
            // simulate this by reusing the same image object
            data[result_at].redefine(from_val, false);
        } else {
            // Construct a new value to serve as result, then copy the result val to it
            dst_val = ret_type->construct();
            // Load from a pointer, which may be a variable
            dst_val->copyFrom(*from_val);
        }
        break;
    }
    case spv::OpStore: {  // 62
        Value* val = getValue(1, data);
        Value& store_to = *getFromPointer(0, data);
        store_to.copyFrom(*val);
        break;
    }
    case spv::OpImageWrite: {  // 99
        Value* image_v = getValue(0, data);
        if (image_v->getType().getBase() != DataType::IMAGE)
            throw std::runtime_error("The third operand to ImageWrite must be an image!");
        auto& image = static_cast<Image&>(*image_v);
        const Value* texel = getValue(2, data);
        // If the texel is a single value, we need to compose it in a temporary array
        const Array* composed;
        if (texel->getType().getBase() == DataType::ARRAY)
            composed = static_cast<const Array*>(texel);
        else {
            // TODO HERE
            throw std::runtime_error("Unimplemented ImageWrite variant!");
        }

        auto [x, y, z, q] = Image::extractCoords(getValue(1, data), image.getDimensionality(), false);
        // We only support int coordinates currently
        auto get = [](float x) {
            if ((1.0 - (std::ceil(x) - x)) != 1.0)
                throw std::runtime_error("Unsupported float coordinates to Image Write!");
            return static_cast<int>(x);
        };
        image.write(get(x), get(y), get(z), *composed);
        break;
    }
    case spv::OpControlBarrier: {  // 224
        blocked = true;
        // TODO surely there is more to do here...
        break;
    }
    case spv::OpPhi: {  // 245
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
    case spv::OpLabel: {  // 248
        Value* val = getValue(0, data);  // get the label value which has been made earlier
        auto prim = static_cast<Primitive*>(val);
        frame.setLabel(prim->data.u32);
        break;
    }
    case spv::OpBranch: {  // 249
        Value* dstv = getValue(0, data);
        Primitive* dst = static_cast<Primitive*>(dstv);
        frame.setPC(dst->data.u32);
        inc_pc = false;
        break;
    }
    case spv::OpBranchConditional: {  // 250
        Value* condv = getValue(0, data);
        Primitive* cond = static_cast<Primitive*>(condv);
        Value* branchv = getValue((cond->data.b32) ? 1 : 2, data);
        Primitive* branch = static_cast<Primitive*>(branchv);
        frame.setPC(branch->data.u32);
        inc_pc = false;
        break;
    }
    case spv::OpKill:  // 252
    case spv::OpTerminateInvocation: {  // 4416
        // Completely stops execution
        terminate_invocation();
        break;
    }
    case spv::OpSwitch: {  // 251
        Value* selectorv = getValue(0, data);
        int selector = static_cast<Primitive*>(selectorv)->data.i32;
        unsigned i = 2;
        for (; i < operands.size(); i += 2) {
            assert(operands[i].type == Token::Type::INT);
            int to_match = std::get<int>(operands[i].raw);
            if (to_match == selector) {
                ++i;
                break;
            }
        }
        if (i >= operands.size())
            i = 1;  // use default case
        Value* dstv = getValue(i, data);
        Primitive* dst = static_cast<Primitive*>(dstv);
        frame.setPC(dst->data.u32);
        inc_pc = false;
        break;
    }
    case spv::OpReturn:  // 253
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        inc_pc = pop_frame();  // don't increment PC if we are at the end of program
        break;
    case spv::OpReturnValue: {  // 254
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
    case spv::OpUnreachable:  // 255
        // Illegal code path reached- signal to user
        throw std::runtime_error("Unreachable code path executed!");
        break;
    case spv::OpTraceRayKHR: {  // 4445
        AccelStruct& as = static_cast<AccelStruct&>(*getValue(0, data));

        // We use the trigger to keep track of rt stage:
        // 1) NONE: first appearance at the instruction
        // 2) INTERSECTION: returned after intersection (which itself may have invoked any hit)
        // 3) ANY_HIT: returned after non-opaque triangle intersection
        // 4) CLOSEST or MISS: return after processing the chosen hit/miss
        auto prev_stage = frame.getRtTrigger();
        if (prev_stage != RtStageKind::MISS && prev_stage != RtStageKind::CLOSEST) {
            Value* hit_attrib = nullptr;
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
                    use_sbt,
                    offset_sbt & 0xF,  // Only the 4 least-significant bits of SBT Offset are used
                    stride_sbt & 0xF,  // Only the 4 least-significant bits of SBT Stride are used
                    miss_index & 0xFFFF  // Only the 16 least-significant bits of Miss Index are used
                );
            } else {
                bool valid_intersect = false;

                if (prev_stage == RtStageKind::INTERSECTION) {
                    // handle the result of the intersection shader
                    hit_attrib = frame.getHitAttribute();
                    Primitive* intersected = static_cast<Primitive*>(frame.getRtResult());
                    valid_intersect = intersected->data.b32;
                    delete intersected;
                } else {
                    assert(prev_stage == RtStageKind::ANY_HIT);
                    // Return from any hit for non-opaque triangle geometry
                    Array& payload = static_cast<Array&>(*frame.getRtResult());
                    valid_intersect = static_cast<Primitive&>(*payload[0]).data.b32;
                    // We ignore payload[1] (continue_search) since it indicates whether we should continue the
                    // intersection shader, which we don't have.
                    delete &payload;
                }

                // In the case of a failed intersection, we will not resume trace (since it failed, there is nothing to
                // resume). In case of a hit, then we want to analyze the hit in traceRay.
                if (!valid_intersect)
                    frame.disableRaytrace();
            }

            // Return whether at least one intersection was made
            Ternary status = as.traceRay(frame.getRtTrigger() != RtStageKind::NONE);
            if (status == Ternary::MAYBE) {
                assert(use_sbt);
                // We need to launch a substage here
                // Type must be triangle or intersection
                invoke_substage_shader(
                    (as.getTrace().getCandidate().type == Intersection::Type::Triangle) ? RtStageKind::ANY_HIT
                                                                                        : RtStageKind::INTERSECTION,
                    frame,
                    as,
                    nullptr
                );
                inc_pc = false;
                break;
            }

            // Payload should either be filled with whether the trace intersected a geometry (a boolean)
            // or the user-defined payload output.
            Variable* payload_var = getVariable(10, data);
            Value* payload = &payload_var->getVal();

            // Do not invoke any shaders if a shader binding table was not specified
            bool used_sbt = false;
            const Trace& trace = as.getTrace();
            if (trace.useSBT) {
                // Otherwise, invoke either the closest hit or miss shaders
                if (trace.hasCommitted()) {
                    // Closest hit
                    if (!trace.rayFlags.skipClosestHitShader()) {
                        invoke_substage_shader(RtStageKind::CLOSEST, frame, as, hit_attrib, payload);
                        used_sbt = true;
                    }
                } else {
                    // Miss
                    if (hit_attrib != nullptr)
                        // Actually don't need the hit attribute to call the miss shader
                        delete hit_attrib;
                    invoke_substage_shader(RtStageKind::MISS, frame, as, nullptr, payload);
                    used_sbt = true;
                }
            }

            // If the expected shader was missing from the SBT or if we shouldn't use the SBT, fill in default
            if (!used_sbt) {
                std::stack<Value*> frontier;
                frontier.push(payload);

                unsigned dummy_idx = -1;
                Intersection sect(nullptr);
                if (status == Ternary::YES)
                    sect = trace.getCommitted();

                while (!frontier.empty()) {
                    Value* curr = frontier.top();
                    frontier.pop();

                    auto base = curr->getType().getBase();
                    if (base == DataType::ARRAY || base == DataType::STRUCT) {
                        auto& agg = static_cast<Aggregate&>(*curr);
                        // Must push elements in reverse order to get read out forwards from stack
                        for (unsigned i = agg.getSize(); i-- > 0;)
                            frontier.push(agg[i]);
                        continue;
                    } else if (!Primitive::isPrimitive(base)) {
                        std::stringstream err;
                        err << "Cannot fill data of unsupported payload type: " << curr->getType().getBase();
                        throw std::runtime_error(err.str());
                    }

                    // Dummy payload holds these fields in this order (how many included dependent on payload size):
                    // - hitT float (uint: reinterpreted hitT, bool: isHit)
                    // - geom_index uint (bool: is non zero)
                    // - prim_index uint (bool: is non zero)
                    // - hit_kind uint (bool: is front)
                    Primitive& val = static_cast<Primitive&>(*curr);
                    ++dummy_idx;

                    switch (dummy_idx) {
                    case 0: {
                        if (base == DataType::FLOAT) {
                            Primitive pfloat(sect.hitT);
                            val.copyFrom(pfloat);
                        } else if (base == DataType::BOOL) {
                            Primitive pbool(sect.isValidHit());
                            val.copyFrom(pbool);
                        } else {
                            Primitive pfloat(sect.hitT);
                            val.copyReinterp(pfloat);
                        }
                        break;
                    }
                    case 1: {
                        Primitive puint(sect.geometryIndex);
                        val.copyFrom(puint);
                        break;
                    }
                    case 2: {
                        Primitive puint(sect.primitiveIndex);
                        val.copyFrom(puint);
                        break;
                    }
                    case 3: {
                        if (base == DataType::BOOL) {
                            Primitive pbool(sect.hitKind == HitKind::FRONT_FACING_TRIANGLE);
                            val.copyFrom(pbool);
                        } else {
                            Primitive puint(sect.hitKind);
                            val.copyFrom(puint);
                        }
                        break;
                    }
                    default: {
                        Primitive zero(0u);
                        Primitive pfalse(false);
                        if (base == DataType::BOOL)
                            val.copyFrom(pfalse);
                        else
                            val.copyFrom(zero);
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
            // Hit attribute generated by the intersection shader (if present)
            if (Value* hit_attrib = frame.getHitAttribute(); hit_attrib != nullptr)
                delete hit_attrib;
        }
        frame.disableRaytrace();
        break;
    }
    case spv::OpExecuteCallableKHR: {  // 4446
        if (!frame.isCallableReturn()) {
            const Primitive& index_sbt_prim = static_cast<const Primitive&>(*getValue(0, data));
            assert(index_sbt_prim.getType().getBase() == DataType::UINT);
            const unsigned index_sbt = index_sbt_prim.data.u32;
            auto& call_data = *getVariable(1, data);
            frame.triggerCallable(index_sbt, &call_data.getVal(), frame.getFromAs());
            // return to this instruction after exit to clean up
            inc_pc = false;
        } else
            frame.disableRaytrace();
        break;
    }
    case spv::OpIgnoreIntersectionKHR:  // 4448
    case spv::OpTerminateRayKHR: {  // 4449
        // OpIgnoreIntersectionKHR: ignores/rejects this potential intersection. Continues intersection searching above
        // OpTerminateRayKHR: Accepts the potential intersection and stops searching above
        // The two are nearly identical. The only difference is which field in the result is changed.
        unsigned field = opcode == spv::OpIgnoreIntersectionKHR ? 0 : 1;

        // First, we have to get the launching frame. It should be the most recent frame with an rt trigger
        Frame* launch_frame = get_launching_frame(frame_stack, RtStageKind::ANY_HIT);
        if (launch_frame != nullptr) {
            Value* result = launch_frame->getRtResult();
            assert(result != nullptr);
            Array& arr = static_cast<Array&>(*result);
            Primitive& to_change = static_cast<Primitive&>(*arr[field]);
            Primitive no(false);
            to_change.copyFrom(no);
        }
        terminate_invocation();
        break;
    }
    case spv::OpCooperativeMatrixLoadKHR: {  // 4457
        Type* result_type = getType(0, data);
        Pointer pointer = *static_cast<Pointer*>(getValue(2, data));
        unsigned back_index = pointer.decompose();
        Value* head = getHeadValue(pointer, data);
        Value* ptr = pointer.dereference(*head);
        bool row_major = static_cast<Primitive*>(getValue(3, data))->data.i32 == 0;

        uint32_t total_elements = result_type->getSize();
        // Split those elements between all in the frame stack
        uint32_t e_beg = (invocation * total_elements) / num_invocations;
        uint32_t e_fin = ((invocation + 1) * total_elements) / num_invocations;

        // The extension spec: https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_cooperative_matrix.html
        uint32_t rows = result_type->getNumRows();
        uint32_t cols = total_elements / rows;

        unsigned stride = row_major ? cols : rows;
        if (operands.size() >= 5) {
            unsigned new_stride = static_cast<Primitive*>(getValue(4, data))->data.u32;
            if (new_stride < stride)
                Console::warn("Given stride is less than the major axis length. Load will read overlapping elements!");
            stride = new_stride;
        }

        CoopMatrix* result = new CoopMatrix(result_type->getElement(), rows, cols);

        std::vector<const Value*> elements;
        for (uint32_t i = e_beg; i < e_fin; ++i) {
            // Pointer can be either a scalar or a vector of the desired type
            const Type& ptr_type = ptr->getType();
            if (ptr_type.getBase() != DataType::ARRAY)
                // Copy the scalar as many times as necessary
                elements.push_back(ptr);
            else {
                // We may need to get a new index:
                // - Decompose the row-major index into a row and column position
                unsigned x = i % cols;
                unsigned y = i / cols;
                // - Compose the position into a flat index, using the specified stride
                unsigned index = row_major ? (y * stride) + x : (x * stride) + y;
                const auto& arr = static_cast<const Array&>(*ptr);
                elements.push_back(arr[back_index + index]);
            }
        }
        result->addElements(elements);
        data[result_at].redefine(result);
        break;
    }
    case spv::OpCooperativeMatrixStoreKHR: {  // 4458
        Pointer pointer = *static_cast<Pointer*>(getValue(0, data));
        unsigned back_index = pointer.decompose();
        Value* head = getHeadValue(pointer, data);
        Value* ptr = pointer.dereference(*head);  // ptr to store data into
        CoopMatrix& mat = *static_cast<CoopMatrix*>(getValue(1, data));
        mat.enforceSize(invocation, num_invocations);
        bool row_major = static_cast<Primitive*>(getValue(2, data))->data.i32 == 0;

        const Type& mat_type = mat.getType();
        uint32_t total_elements = mat_type.getSize();
        // Split those elements between all in the frame stack
        uint32_t e_beg = (invocation * total_elements) / num_invocations;

        uint32_t rows = mat_type.getNumRows();
        uint32_t cols = total_elements / rows;

        unsigned stride = row_major ? cols : rows;
        if (operands.size() >= 4) {
            unsigned new_stride = static_cast<Primitive*>(getValue(3, data))->data.u32;
            if (new_stride < stride)
                Console::warn(
                    "Given stride is less than the major axis length. Store will write overlapping elements!"
                );
            stride = new_stride;
        }

        // Pointer can be either a scalar or a vector of the desired type
        const Type& ptr_type = ptr->getType();
        if (ptr_type.getBase() != DataType::ARRAY)
            // Copy the scalar value
            ptr->copyFrom(*mat[mat.getSize() - 1]);
        else {
            auto& arr = static_cast<Array&>(*ptr);
            for (uint32_t j = 0; j < mat.getSize(); ++j) {
                uint32_t i = e_beg + j;
                // We may need to get a new index:
                // - Decompose the row-major index into a row and column position
                unsigned x = i % cols;
                unsigned y = i / cols;
                // - Compose the position into a flat index, using the specified stride
                unsigned index = row_major ? (y * stride) + x : (x * stride) + y;
                arr[back_index + index]->copyFrom(*mat[j]);
            }
        }
        break;
    }
    case spv::OpCooperativeMatrixMulAddKHR: {  // 4459
        // A * B + C, where
        // - A has M rows and K columns
        // - B has K rows and N columns
        // - C has M rows and N columns

        // (MxK) * (KxN) + (MxN)
        // ⎡ a0 a1 a2 a3 ⎤   ⎡ b0 b1 b2 ⎤   ⎡ c0 c1 c2 ⎤
        // ⎢ a4 a5 a6 a7 ⎥ * ⎢ b3 b4 b5 ⎥ + ⎢ c3 c4 c5 ⎥
        // ⎣ a8 a9 a0 a1 ⎦   ⎢ b6 b7 b8 ⎥   ⎣ c6 c7 c8 ⎦
        //                   ⎣ b9 b0 b1 ⎦

        // Cooperative Matrices distribute the matrix elements across invocations. To compute the matrix multplication,
        // we will have to reach this dispersed data.
        Type& res_type = *getType(0, data);
        auto& amat = static_cast<CoopMatrix&>(*getValue(2, data));
        amat.enforceSize(invocation, num_invocations);
        auto& bmat = static_cast<CoopMatrix&>(*getValue(3, data));
        bmat.enforceSize(invocation, num_invocations);
        auto& cmat = static_cast<CoopMatrix&>(*getValue(4, data));
        cmat.enforceSize(invocation, num_invocations);

        CoopMatrix& result = static_cast<CoopMatrix&>(*res_type.construct());
        dst_val = &result;

        // Determine which indices in the result matrix need to be populated by this invocation
        uint32_t res_total_elements = res_type.getSize();
        uint32_t e_beg = (invocation * res_total_elements) / num_invocations;
        uint32_t e_fin = ((invocation + 1) * res_total_elements) / num_invocations;

        unsigned result_num_rows = amat.getNumRows();
        unsigned shared_dim = bmat.getNumRows();
        unsigned result_num_cols = res_type.getSize() / result_num_rows;

        std::vector<const Value*> elements;
        std::vector<Primitive> prims;
        prims.reserve(e_fin - e_beg);
        const Type& element_type = res_type.getElement();
        // Perform the matrix multiplication first
        for (unsigned i = e_beg; i < e_fin; ++i) {
            unsigned result_row = i / result_num_cols;
            unsigned result_col = i % result_num_cols;

            // TODO: need to support other element types besides float
            assert(element_type.getBase() == DataType::FLOAT);
            double accum = 0.0;

            for (unsigned j = 0; j < shared_dim; ++j) {
                auto extract_coop_el =
                    [&frame_stacks, num_invocations, this](unsigned idx, unsigned opnd) -> const Primitive* {
                    unsigned found = 0;
                    for (unsigned k = 0; k < num_invocations; ++k) {
                        auto& data = frame_stacks[k].back()->getData();
                        const auto& mat = static_cast<const CoopMatrix&>(*getValue(opnd, data));
                        if (unsigned next = found + mat.getSize(); next <= idx)
                            found = next;
                        else
                            return static_cast<const Primitive*>(mat[idx - found]);
                    }
                    assert(false);
                    return nullptr;
                };

                // - from a: (col = j, row = result_row)
                // - from b: (col = result_col, row = j)
                const Primitive* a_el = extract_coop_el((result_row * shared_dim) + j, 2);
                const Primitive* b_el = extract_coop_el((j * result_num_cols) + result_col, 3);
                accum += a_el->data.fp32 * b_el->data.fp32;
            }

            // Now add with the accumulator matrix. Since it has the same dimensions as the result, we know that not
            // only is the necessary value within the same invocation's data, but even the same index.
            accum += static_cast<const Primitive&>(*cmat[i - e_beg]).data.fp32;

            // Finally, create the primitive and add it to pending elements
            elements.push_back(&prims.emplace_back(static_cast<float>(accum)));
        }
        result.addElements(elements);
        break;
    }
    case spv::OpCooperativeMatrixLengthKHR: {  // 4460
        Type& mat_type = *getType(2, data);
        uint32_t total_elements = mat_type.getSize();
        // Split those elements between all in the frame stack
        uint32_t e_beg = (invocation * total_elements) / num_invocations;
        uint32_t e_fin = ((invocation + 1) * total_elements) / num_invocations;
        Primitive len(e_fin - e_beg);
        dst_val = getType(0, data)->construct();
        dst_val->copyFrom(len);
        break;
    }
    case spv::OpRayQueryInitializeKHR: {  // 4473
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
            // Note: ray query does NOT support using the shader binding table
            ray_flags,
            cull_mask & 0xFF,
            ray_origin,
            ray_direction,
            ray_t_min,
            ray_t_max,
            false
        );
        break;
    }
    case spv::OpRayQueryTerminateKHR: {  // 4474
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        ray_query.getAccelStruct().terminate();
        break;
    }
    case spv::OpRayQueryGenerateIntersectionKHR: {  // 4475
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        const float t_hit = static_cast<Primitive&>(*getValue(1, data)).data.fp32;
        ray_query.getAccelStruct().generateIntersection(t_hit);
        break;
    }
    case spv::OpRayQueryConfirmIntersectionKHR: {  // 4476
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        ray_query.getAccelStruct().confirmIntersection();
        break;
    }
    case spv::OpRayQueryProceedKHR: {  // 4477
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        AccelStruct& as = ray_query.getAccelStruct();

        Ternary status = as.stepTrace();
        assert(status != Ternary::MAYBE);  // ray query cannot handle sbt, so it should have that disabled

        if (status == Ternary::YES && as.getTrace().rayFlags.terminateOnFirstHit())
            as.terminate();

        dst_val = new Primitive(status == Ternary::YES);
        break;
    }
    case spv::OpReportIntersectionKHR: {  // 5334
        const float t_hit = static_cast<Primitive&>(*getValue(2, data)).data.fp32;

        auto prev_stage = frame.getRtTrigger();
        bool valid_intersect = false;
        bool continue_search = true;
        float t_min = 0.0;
        Frame* launch_frame = get_launching_frame(frame_stack, RtStageKind::INTERSECTION);
        if (prev_stage == RtStageKind::NONE) {
            // Get data from the ray tracing pipeline if it exists (won't exist if this is run in a dummy pipeline)
            AccelStruct* accel_struct = nullptr;
            if (launch_frame != nullptr) {
                accel_struct = launch_frame->getAccelStruct();
                assert(accel_struct != nullptr);
                const auto& trace = accel_struct->getTrace();
                t_min = trace.rayTMin;

                // Invoke the any hit shader if running a ray tracing pipeline and the geometry is non-opaque
                if (!trace.getCandidate().isOpaque) {
                    invoke_substage_shader(RtStageKind::ANY_HIT, frame, *accel_struct, launch_frame->getHitAttribute());
                    inc_pc = false;
                    break;
                }
            }
            // When not running the any hit, assume validity within the range
            valid_intersect = t_hit >= t_min;
        } else {
            // We have returned from the any hit shader. Handle its results
            Array& payload = static_cast<Array&>(*frame.getRtResult());
            valid_intersect = static_cast<Primitive&>(*payload[0]).data.b32;
            continue_search = static_cast<Primitive&>(*payload[1]).data.b32;

            frame.disableRaytrace();
            delete &payload;
        }

        if (launch_frame != nullptr) {
            if (valid_intersect) {
                AccelStruct& as = *launch_frame->getAccelStruct();
                const unsigned hit_kind = static_cast<Primitive&>(*getValue(3, data)).data.u32;
                Intersection& candidate = as.getCandidate();
                candidate.hitKind = HitKind(hit_kind);
                candidate.hitT = t_hit;
            }

            Primitive* intersect_result = static_cast<Primitive*>(launch_frame->getRtResult());
            assert(intersect_result != nullptr);
            Primitive prim(valid_intersect);
            intersect_result->copyFrom(prim);
        }

        dst_val = getType(0, data)->construct();
        Primitive prim(valid_intersect);
        dst_val->copyFrom(prim);

        if (!continue_search)
            terminate_invocation();
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
