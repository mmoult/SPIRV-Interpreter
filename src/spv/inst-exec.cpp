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
import value.accelerationStructure;
import value.aggregate;
import value.primitive;
import value.rayQuery;

void Instruction::execute(DataView& data, std::vector<Frame*>& frame_stack, bool verbose) const {
    bool inc_pc = true;
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

        frame_stack.push_back(new Frame(fx->getLocation(), args, result_at, *data.getSource()));
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
        // TODO: change me once the interpreter supports shader invocation.
        // Currently, run the ray through an implementation of a standard ray tracing pipeline

        // --- Assertions
        assert(getValue(0, data)->getType().getBase() == DataType::RAY_TRACING_ACCELERATION_STRUCTURE);
        for (unsigned i = 1; i < 6; ++i)
            assert(getValue(i, data)->getType().getBase() == DataType::UINT);
        assert(getValue(6, data)->getType().getBase() == DataType::ARRAY);
        assert(getValue(7, data)->getType().getBase() == DataType::FLOAT);
        assert(getValue(8, data)->getType().getBase() == DataType::ARRAY);
        assert(getValue(9, data)->getType().getBase() == DataType::FLOAT);

        // --- Gather arguments to instruction
        AccelerationStructureManager& as = static_cast<AccelerationStructureManager&>(*getValue(0, data));

        unsigned rayFlags = static_cast<Primitive&>(*getValue(1, data)).data.u32;
        unsigned cullMask = static_cast<Primitive&>(*getValue(2, data)).data.u32;
        unsigned offsetSBT = static_cast<Primitive&>(*getValue(3, data)).data.u32;
        unsigned strideSBT = static_cast<Primitive&>(*getValue(4, data)).data.u32;
        unsigned missIndex = static_cast<Primitive&>(*getValue(5, data)).data.u32;

        Array& rayOriginInfo = static_cast<Array&>(*getValue(6, data));  // TODO: is it possible that values could be integers?
        std::vector<float> rayOrigin;
        for (unsigned i = 0; i < rayOriginInfo.getSize(); ++i) {
            rayOrigin.push_back(static_cast<Primitive&>(*(rayOriginInfo[i])).data.fp32);
        }

        float rayTMin = static_cast<Primitive&>(*getValue(7, data)).data.fp32;

        Array& rayDirectionInfo = static_cast<Array&>(*getValue(8, data));  // TODO: is it possible that values could be integers?
        std::vector<float> rayDirection;
        for (unsigned i = 0; i < rayDirectionInfo.getSize(); ++i) {
            rayDirection.push_back(static_cast<Primitive&>(*(rayDirectionInfo[i])).data.fp32);
        }

        assert(rayOrigin.size() == rayDirection.size());  // Same dimension?

        float rayTMax = static_cast<Primitive&>(*getValue(9, data)).data.fp32;

        auto payloadPointer = getFromPointer(10, data);

        // --- Execute instruction
        // Run it through our implementation of a ray tracing pipeline
        // Only the 8 least-significant bits of Cull Mask are used in this instruction
        // Only the 4 least-significant bits of SBT Offset are used in this instruction
        // Only the 4 least-significant bits of SBT Stride are used in this instruction
        // Only the 16 least-significant bits of Miss Index are used in this instruction
        bool didIntersectGeometry;
        as.traceRay(didIntersectGeometry,
                rayFlags,
                cullMask & 0xFF,
                rayOrigin,
                rayDirection,
                rayTMin,
                rayTMax,
                true,
                offsetSBT & 0xF,
                strideSBT & 0xF,
                missIndex & 0xFFFF);

        // Store the data into the payload

        // TODO: currently, payload stores if a geometry was intersected (a boolean).
        // Will need to change once the interpreter is capable to invoking other shaders.
        // Note that payload is user-defined, so it is important that trace ray invokes
        // other shaders (notably user-defined) which will correctly fill the payload.
        as.fillPayloadWithBool(payloadPointer, didIntersectGeometry);

        break;
    }
    case spv::OpExecuteCallableKHR: { // 4446
        // TODO: call the callable shader once interpreter can invoke multiple shaders
        unsigned indexSBT = static_cast<Primitive&>(*getValue(0, data)).data.u32;
        auto shaderArguments = getFromPointer(1, data);
        std::cout << "WARNING: OpExecuteCallableKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "Invoking callable shader at SBT index = (" << indexSBT << ") with argument of type ("
                  << shaderArguments->getType().getBase() << ")" << std::endl;
        break;
    }
    case spv::OpConvertUToAccelerationStructureKHR: {
        // TODO: needs the get an acceleration structure from a buffer via a 64-bit address
        // TODO: currently does not support uint64_t
        std::cout << "WARNING: OpConvertUToAccelerationStructureKHR instruction does nothing as the moment!" << std::endl;
        Value* addressPointer = getValue(2, data);
        assert(addressPointer != nullptr);
        uint64_t address = 0;
        if (addressPointer->getType().getBase() == DataType::ARRAY) {
            // case uvec2
            Array& addressComponents = static_cast<Array&>(*addressPointer);
            assert(addressComponents.getSize() == 2);
            address = static_cast<Primitive&>(*(addressComponents[0])).data.u32;
            address <<= 32;
            uint32_t lower = static_cast<Primitive&>(*(addressComponents[1])).data.u32;
            address |= lower;
        } else {
            // case uint64_t
            throw std::runtime_error("uint64_t is unsupported for OpConvertUToAccelerationStructureKHR.");
        }
        std::cout << "\taddress = " << address << std::endl;
                
        // Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        AccelerationStructureManager& result = static_cast<AccelerationStructureManager&>(*getValue(1, data));

        // TODO: set the acceleration structure
        throw std::runtime_error("OpConvertUToAccelerationStructureKHR not implemented.");
        break;
    }
    case spv::OpIgnoreIntersectionKHR: { // 4448
        // TODO: update once interpreter supports multi-shader invocation
        std::cout << "WARNING: OpIgnoreIntersectionKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "\tShould terminate the calling any-hit shader and continue ray traversal without modifying "
                     "gl_RayTmaxEXT and gl_RayTminEXT."
                  << std::endl;

        // TODO: temporarily do what OpReturn does
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        inc_pc = pop_frame(); // don't increment PC if we are at the end of program

        break;
    }
    case spv::OpTerminateRayKHR: { // 4449
        // TODO: update once interpreter supports multi-shader invocation
        std::cout << "WARNING: OpTerminateRayKHR instruction does nothing as the moment!" << std::endl;
        std::cout << "\tShould stop the ray traversal and invoke the closest hit shader." << std::endl;

        // TODO: temporarily do what OpReturn does
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        inc_pc = pop_frame(); // don't increment PC if we are at the end of program

        break;
    }
    case spv::OpRayQueryInitializeKHR: { // 4473
        // --- Assertions
        assert(getFromPointer(0, data)->getType().getBase() == DataType::RAY_QUERY);
        assert(getValue(1, data)->getType().getBase() == DataType::RAY_TRACING_ACCELERATION_STRUCTURE);
        assert(getValue(2, data)->getType().getBase() == DataType::UINT);
        assert(getValue(3, data)->getType().getBase() == DataType::UINT);
        assert(getValue(4, data)->getType().getBase() == DataType::ARRAY);
        assert(getValue(5, data)->getType().getBase() == DataType::FLOAT);
        assert(getValue(6, data)->getType().getBase() == DataType::ARRAY);
        assert(getValue(7, data)->getType().getBase() == DataType::FLOAT);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(0, data));
        AccelerationStructureManager& as = static_cast<AccelerationStructureManager&>(*getValue(1, data));
        unsigned rayFlags = static_cast<Primitive&>(*getValue(2, data)).data.u32;
        unsigned cullMask = static_cast<Primitive&>(*getValue(3, data)).data.u32;

        Array& rayOriginInfo = static_cast<Array&>(*getValue(4, data));  // TODO: is it possible that values could be integers?
        std::vector<float> rayOrigin;
        for (unsigned i = 0; i < rayOriginInfo.getSize(); ++i) {
            rayOrigin.push_back(static_cast<Primitive&>(*(rayOriginInfo[i])).data.fp32);
        }

        float rayTMin = static_cast<Primitive&>(*getValue(5, data)).data.fp32;

        Array& rayDirectionInfo = static_cast<Array&>(*getValue(6, data));  // TODO: is it possible that values could be integers?
        std::vector<float> rayDirection;
        for (unsigned i = 0; i < rayDirectionInfo.getSize(); ++i) {
            rayDirection.push_back(static_cast<Primitive&>(*(rayDirectionInfo[i])).data.fp32);
        }

        assert(rayOrigin.size() == rayDirection.size());  // Same dimension?

        float rayTMax = static_cast<Primitive&>(*getValue(7, data)).data.fp32;

        // Initialize the ray query
        rayQuery.initialize(as, rayFlags, cullMask, rayOrigin, rayDirection, rayTMin, rayTMax);

        break;
    }
    case spv::OpRayQueryTerminateKHR: { // 4474
        // --- Assertions
        assert(getFromPointer(0, data) != nullptr &&
                getFromPointer(0, data)->getType().getBase() == DataType::RAY_QUERY);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(0, data));

        // --- Terminate the given ray query
        rayQuery.terminate();

        break;
    }
    case spv::OpRayQueryProceedKHR: { // 4477
        // --- Assertions
        assert(getFromPointer(2, data) != nullptr &&
                getFromPointer(2, data)->getType().getBase() == DataType::RAY_QUERY);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(2, data));

        // Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Primitive& result = static_cast<Primitive&>(*getValue(1, data));

        // --- Step through traversal
        result.data.b32 = rayQuery.proceed();

        break;
    }
    case spv::OpRayQueryGetIntersectionTypeKHR: { // 4479
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(2, data));
        unsigned intersection = static_cast<Primitive&>(*getValue(3, data)).data.u32; // candidate(0) or committed()

        std::cout << "intersection value = " << intersection << std::endl;

        // Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Primitive& result = static_cast<Primitive&>(*getValue(1, data));

        result.data.u32 = rayQuery.getIntersectionType(intersection);

        break;
    }
    case spv::OpReportIntersectionKHR: { // 5334
        // TODO: update once interpreter supports multi-shader invocation
        // Get intersection information
        std::cout << "WARNING: OpReportIntersectionKHR instruction does not follow specifications at the moment!" << std::endl;
        const float hitT = static_cast<Primitive&>(*getValue(2, data)).data.fp32;
        const unsigned hitKind = static_cast<Primitive&>(*getValue(3, data)).data.u32;
        std::cout << "Intersection shader reported an intersection with hitT = (" << hitT << ") and hitKind = ("
                  << hitKind << ")" << std::endl;

        // Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Primitive& result = static_cast<Primitive&>(*getValue(1, data));
        result.data.b32 = false;

        // TODO: once intersection shader can be invoked by pipeline, use actual rayTMin and rayTMax.
        // For now, using constants.
        const float rayTMin = 0.0;
        const float rayTMax = std::numeric_limits<float>::infinity();

        if (hitT < rayTMin || hitT > rayTMax) {
            // Intersection is outside of the current ray interval
            std::cout << "\tRay missed; hitT was not in the range [" << rayTMin << ", " << rayTMax << "]" << std::endl;
            result.data.b32 = false;
        } else {
            // TODO: Invoke any-hit shader.
            // If ignored by any-hit, return false.
            // If any-hit rejects it, return false.
            std::cout << "\t(Not working right now) Invoking any-hit shader..." << std::endl;
            std::cout << "\tSuccessful any-hit shader" << std::endl;
            result.data.b32 = true;
        }

        break;
    }
    case spv::OpRayQueryGetRayTMinKHR: { // 6016
        // --- Assertions
        assert(getFromPointer(2, data) != nullptr &&
                getFromPointer(2, data)->getType().getBase() == DataType::RAY_QUERY);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(2, data));

        // --- Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Primitive& result = static_cast<Primitive&>(*getValue(1, data));

        // --- Store the return value
        result.data.fp32 = rayQuery.getRayTMin();

        break;
    }
    case spv::OpRayQueryGetRayFlagsKHR: { // 6017
        // --- Assertions
        assert(getFromPointer(2, data) != nullptr &&
                getFromPointer(2, data)->getType().getBase() == DataType::RAY_QUERY);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(2, data));

        // --- Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Primitive& result = static_cast<Primitive&>(*getValue(1, data));

        // --- Store the return value
        result.data.u32 = rayQuery.getRayFlags();

        break;
    }
    case spv::OpRayQueryGetWorldRayDirectionKHR: { // 6029
        // --- Assertions
        assert(getFromPointer(2, data) != nullptr &&
                getFromPointer(2, data)->getType().getBase() == DataType::RAY_QUERY);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(2, data));
        
        // --- Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Array& result = static_cast<Array&>(*getValue(1, data));
        assert(result.getSize() == 3);

        // --- Store the return value
        const auto origin = rayQuery.getWorldRayDirection();
        for (unsigned i = 0; i < result.getSize(); ++i) {
            Primitive& location = static_cast<Primitive&>(*(result[i]));
            location.data.fp32 = origin[i];
        }

        break;
    }
    case spv::OpRayQueryGetWorldRayOriginKHR: { // 6030
        // --- Assertions
        assert(getFromPointer(2, data) != nullptr &&
                getFromPointer(2, data)->getType().getBase() == DataType::RAY_QUERY);

        // --- Get the arguments
        RayQuery& rayQuery = static_cast<RayQuery&>(*getFromPointer(2, data));
        
        // --- Set up the return type
        makeResult(data, 1, nullptr); // location and queue does not matter
        Array& result = static_cast<Array&>(*getValue(1, data));
        assert(result.getSize() == 3);

        // --- Store the return value
        const auto origin = rayQuery.getWorldRayOrigin();
        for (unsigned i = 0; i < result.getSize(); ++i) {
            Primitive& location = static_cast<Primitive&>(*(result[i]));
            location.data.fp32 = origin[i];
        }

        break;
    }
    }

    if (dst_val != nullptr) {
        assert(hasResult);
        data[result_at].redefine(dst_val);
    }

    if (inc_pc)
        frame_stack.back()->incPC();
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
