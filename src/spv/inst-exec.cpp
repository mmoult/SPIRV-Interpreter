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
#include <string>
#include <variant>
#include <vector>

#define SPV_ENABLE_UTILITY_CODE 1
#include "../external/spirv.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "data/manager.h"
module spv.instruction;
import spv.data.data;
import spv.frame;
import spv.token;
import value.accelStruct;
import value.aggregate;
import value.primitive;
import value.rayQuery;

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

    // Pops the current frame and returns whether there is any more frames after
    auto pop_frame = [&frame_stack]() {
        delete frame_stack.back();
        frame_stack.pop_back();
        return !frame_stack.empty();
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

        frame_stack.push_back(new Frame(fx->getLocation(), args, result_at, *data.getPrev()));
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
        // Construct a new value to serve as result, then copy the resultval to it
        dst_val = ret_type->construct();
        // Load from a pointer, which may be a variable
        const Value* from_val = getFromPointer(2, data);
        dst_val->copyFrom(*from_val);
        break;
    }
    case spv::OpStore: { // 62
        Value* val = getValue(1, data);
        Value* store_to = getFromPointer(0, data);
        store_to->copyFrom(*val);
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
        // --- Get the arguments
        AccelStructManager& as = static_cast<AccelStructManager&>(*getValue(0, data));

        const unsigned ray_flags = static_cast<Primitive&>(*getValue(1, data)).data.u32;
        const unsigned cull_mask = static_cast<Primitive&>(*getValue(2, data)).data.u32;
        const unsigned offset_sbt = static_cast<Primitive&>(*getValue(3, data)).data.u32;
        const unsigned stride_sbt = static_cast<Primitive&>(*getValue(4, data)).data.u32;
        const unsigned miss_index = static_cast<Primitive&>(*getValue(5, data)).data.u32;

        Array& ray_origin_info = static_cast<Array&>(*getValue(6, data));
        std::vector<float> ray_origin;
        for (unsigned i = 0; i < ray_origin_info.getSize(); ++i)
            ray_origin.push_back(static_cast<Primitive&>(*(ray_origin_info[i])).data.fp32);

        const float ray_t_min = static_cast<Primitive&>(*getValue(7, data)).data.fp32;

        Array& ray_direction_info = static_cast<Array&>(*getValue(8, data));
        std::vector<float> rayDirection;
        for (unsigned i = 0; i < ray_direction_info.getSize(); ++i)
            rayDirection.push_back(static_cast<Primitive&>(*(ray_direction_info[i])).data.fp32);

        assert(ray_origin.size() == rayDirection.size());

        const float ray_t_max = static_cast<Primitive&>(*getValue(9, data)).data.fp32;

        auto payload_pointer = getFromPointer(10, data);

        // --- Execute instruction
        // Run it through our implementation of a ray tracing pipeline
        // Only the 8 least-significant bits of Cull Mask are used in this instruction
        // Only the 4 least-significant bits of SBT Offset are used in this instruction
        // Only the 4 least-significant bits of SBT Stride are used in this instruction
        // Only the 16 least-significant bits of Miss Index are used in this instruction
        const bool intersect_geometry = as.traceRay(
            ray_flags,
            cull_mask & 0xFF,
            ray_origin,
            rayDirection,
            ray_t_min,
            ray_t_max,
            true,
            offset_sbt & 0xF,
            stride_sbt & 0xF,
            miss_index & 0xFFFF
        );

        // --- Store the data into the payload

        // TODO: currently, payload stores if a geometry was intersected (a boolean).
        // Must update once SBTs are implemented.
        as.fillPayloadWithBool(payload_pointer, intersect_geometry);

        break;
    }
    case spv::OpExecuteCallableKHR: { // 4446
        // TODO: call the callable shader once there is SBT support
        const unsigned index_sbt = static_cast<Primitive&>(*getValue(0, data)).data.u32;
        const auto shader_args = getFromPointer(1, data);
        std::cout << "WARNING: OpExecuteCallableKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "Invoking callable shader at SBT index = (" << index_sbt << ") with argument of type ("
                  << shader_args->getType().getBase() << ")" << std::endl;
        break;
    }
    case spv::OpIgnoreIntersectionKHR: { // 4448
        // TODO: update once interpreter supports SBTs
        std::cout << "WARNING: OpIgnoreIntersectionKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "\tShould terminate the calling any-hit shader and continue ray traversal without modifying "
                     "gl_RayTmaxEXT and gl_RayTminEXT."
                  << std::endl;

        // TODO: temporarily do what OpReturn does
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        inc_pc = pop_frame();  // don't increment PC if we are at the end of program

        break;
    }
    case spv::OpTerminateRayKHR: { // 4449
        // TODO: update once interpreter supports SBTs
        std::cout << "WARNING: OpTerminateRayKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "\tShould stop the ray traversal and invoke the closest hit shader." << std::endl;

        // TODO: temporarily do what OpReturn does
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        inc_pc = pop_frame();  // don't increment PC if we are at the end of program

        break;
    }
    case spv::OpRayQueryInitializeKHR: { // 4473
        // --- Get the arguments
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        AccelStructManager& as = static_cast<AccelStructManager&>(*getValue(1, data));
        const unsigned ray_flags = static_cast<Primitive&>(*getValue(2, data)).data.u32;
        const unsigned cull_mask = static_cast<Primitive&>(*getValue(3, data)).data.u32;

        Array& ray_origin_info = static_cast<Array&>(*getValue(4, data));
        std::vector<float> ray_origin;
        for (unsigned i = 0; i < ray_origin_info.getSize(); ++i)
            ray_origin.push_back(static_cast<Primitive&>(*(ray_origin_info[i])).data.fp32);

        const float ray_t_min = static_cast<Primitive&>(*getValue(5, data)).data.fp32;

        Array& ray_direction_info = static_cast<Array&>(*getValue(6, data));
        std::vector<float> ray_direction;
        for (unsigned i = 0; i < ray_direction_info.getSize(); ++i)
            ray_direction.push_back(static_cast<Primitive&>(*(ray_direction_info[i])).data.fp32);

        assert(ray_origin.size() == ray_direction.size());

        const float rayTMax = static_cast<Primitive&>(*getValue(7, data)).data.fp32;

        // --- Initialize the ray query
        // Only 8-least significant bits of cull mask are used.
        ray_query.initialize(as, ray_flags, cull_mask & 0xFF, ray_origin, ray_direction, ray_t_min, rayTMax);

        break;
    }
    case spv::OpRayQueryTerminateKHR: { // 4474
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        ray_query.terminate();
        break;
    }
    case spv::OpRayQueryGenerateIntersectionKHR: { // 4475
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        const float hit_t = static_cast<Primitive&>(*getValue(1, data)).data.fp32;
        ray_query.generateIntersection(hit_t);
        break;
    }
    case spv::OpRayQueryConfirmIntersectionKHR: { // 4476
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(0, data));
        ray_query.confirmIntersection();
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
