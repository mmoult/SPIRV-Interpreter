/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "../external/spirv.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
module instruction;
import data;
import frame;
import token;
import value.primitive;

void Spv::Instruction::execute(std::vector<Data>& data, std::vector<Frame>& frame_stack, bool verbose) const {
    bool inc_pc = true;
    Frame& frame = frame_stack.back();

    if (verbose)
        print();

    unsigned result_at;
    if (hasResult) {
        unsigned idx = hasResultType? 1: 0;
        assert(operands[idx].type == Token::Type::REF);
        result_at = std::get<unsigned>(operands[idx].raw);
    }

    Value* dst_val = nullptr;
    switch (opcode) {
    default:
        // fall back on the makeResult function (no fallback should use location!)
        if (!makeResult(data, 0, nullptr)) {
            std::stringstream err;
            err << "Unsupported instruction execution (" << printOpcode(opcode) << ")!";
            throw std::runtime_error(err.str());
        }
        // If the instruction did make a result, success! These instructions cannot modify control flow,
        // so assume inc_pc = true
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

        frame_stack.emplace_back(fx->getLocation(), args, result_at);
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
        while (!frame_stack.empty())
            frame_stack.pop_back();
        inc_pc = false;
        break;
    }
    case spv::OpReturn: // 253
        // verify that the stack didn't expect a return value
        if (frame.hasReturn())
            throw std::runtime_error("Missing value for function return!");
        frame_stack.pop_back();
        inc_pc = !frame_stack.empty(); // don't increment PC if we are at the end of program
        break;
    case spv::OpReturnValue: { // 254
        if (!frame.hasReturn())
            throw std::runtime_error("Void function tried to return a value!");
        Value* val = getValue(0, data);
        // For correctness, we must clone. Consider the case where the return of some function is passed as an argument
        // to another call of the same function. The return could be (re)defined before the argument is used.
        Value* ret = val->getType().construct();
        ret->copyFrom(*val);
        data[frame.getReturn()].redefine(ret);
        frame_stack.pop_back();
        inc_pc = !frame_stack.empty();
        break;
    }
    }

    if (dst_val != nullptr) {
        assert(hasResult);
        data[result_at].redefine(dst_val);
    }

    if (inc_pc)
        frame_stack.back().incPC();
}

std::string Spv::Instruction::printOpcode(spv::Op opcode) {
#define CASE_OP(OP) case spv::OP: \
    return #OP;

    switch (opcode) {
    CASE_OP(OpNop);
    CASE_OP(OpUndef);
    CASE_OP(OpSourceContinued);
    CASE_OP(OpSource);
    CASE_OP(OpSourceExtension);
    CASE_OP(OpName);
    CASE_OP(OpMemberName);
    CASE_OP(OpString);
    CASE_OP(OpLine);
    CASE_OP(OpExtension);
    CASE_OP(OpExtInstImport);
    CASE_OP(OpExtInst);
    CASE_OP(OpMemoryModel);
    CASE_OP(OpEntryPoint);
    CASE_OP(OpExecutionMode);
    CASE_OP(OpCapability);
    CASE_OP(OpTypeVoid);
    CASE_OP(OpTypeBool);
    CASE_OP(OpTypeInt);
    CASE_OP(OpTypeFloat);
    CASE_OP(OpTypeVector);
    CASE_OP(OpTypeMatrix);
    CASE_OP(OpTypeImage);
    CASE_OP(OpTypeSampler);
    CASE_OP(OpTypeSampledImage);
    CASE_OP(OpTypeArray);
    CASE_OP(OpTypeRuntimeArray);
    CASE_OP(OpTypeStruct);
    CASE_OP(OpTypeOpaque);
    CASE_OP(OpTypePointer);
    CASE_OP(OpTypeFunction);
    CASE_OP(OpTypeEvent);
    CASE_OP(OpTypeDeviceEvent);
    CASE_OP(OpTypeReserveId);
    CASE_OP(OpTypeQueue);
    CASE_OP(OpTypePipe);
    CASE_OP(OpTypeForwardPointer);
    CASE_OP(OpConstantTrue);
    CASE_OP(OpConstantFalse);
    CASE_OP(OpConstant);
    CASE_OP(OpConstantComposite);
    CASE_OP(OpConstantSampler);
    CASE_OP(OpConstantNull);
    CASE_OP(OpSpecConstantTrue);
    CASE_OP(OpSpecConstantFalse);
    CASE_OP(OpSpecConstant);
    CASE_OP(OpSpecConstantComposite);
    CASE_OP(OpSpecConstantOp);
    CASE_OP(OpFunction);
    CASE_OP(OpFunctionParameter);
    CASE_OP(OpFunctionEnd);
    CASE_OP(OpFunctionCall);
    CASE_OP(OpVariable);
    CASE_OP(OpImageTexelPointer);
    CASE_OP(OpLoad);
    CASE_OP(OpStore);
    CASE_OP(OpCopyMemory);
    CASE_OP(OpCopyMemorySized);
    CASE_OP(OpAccessChain);
    CASE_OP(OpInBoundsAccessChain);
    CASE_OP(OpPtrAccessChain);
    CASE_OP(OpArrayLength);
    CASE_OP(OpGenericPtrMemSemantics);
    CASE_OP(OpInBoundsPtrAccessChain);
    CASE_OP(OpDecorate);
    CASE_OP(OpMemberDecorate);
    CASE_OP(OpDecorationGroup);
    CASE_OP(OpGroupDecorate);
    CASE_OP(OpGroupMemberDecorate);
    CASE_OP(OpVectorExtractDynamic);
    CASE_OP(OpVectorInsertDynamic);
    CASE_OP(OpVectorShuffle);
    CASE_OP(OpCompositeConstruct);
    CASE_OP(OpCompositeExtract);
    CASE_OP(OpCompositeInsert);
    CASE_OP(OpCopyObject);
    CASE_OP(OpTranspose);
    CASE_OP(OpSampledImage);
    CASE_OP(OpImageSampleImplicitLod);
    CASE_OP(OpImageSampleExplicitLod);
    CASE_OP(OpImageSampleDrefImplicitLod);
    CASE_OP(OpImageSampleDrefExplicitLod);
    CASE_OP(OpImageSampleProjImplicitLod);
    CASE_OP(OpImageSampleProjExplicitLod);
    CASE_OP(OpImageSampleProjDrefImplicitLod);
    CASE_OP(OpImageSampleProjDrefExplicitLod);
    CASE_OP(OpImageFetch);
    CASE_OP(OpImageGather);
    CASE_OP(OpImageDrefGather);
    CASE_OP(OpImageRead);
    CASE_OP(OpImageWrite);
    CASE_OP(OpImage);
    CASE_OP(OpImageQueryFormat);
    CASE_OP(OpImageQueryOrder);
    CASE_OP(OpImageQuerySizeLod);
    CASE_OP(OpImageQuerySize);
    CASE_OP(OpImageQueryLod);
    CASE_OP(OpImageQueryLevels);
    CASE_OP(OpImageQuerySamples);
    CASE_OP(OpConvertFToU);
    CASE_OP(OpConvertFToS);
    CASE_OP(OpConvertSToF);
    CASE_OP(OpConvertUToF);
    CASE_OP(OpUConvert);
    CASE_OP(OpSConvert);
    CASE_OP(OpFConvert);
    CASE_OP(OpQuantizeToF16);
    CASE_OP(OpConvertPtrToU);
    CASE_OP(OpSatConvertSToU);
    CASE_OP(OpSatConvertUToS);
    CASE_OP(OpConvertUToPtr);
    CASE_OP(OpPtrCastToGeneric);
    CASE_OP(OpGenericCastToPtr);
    CASE_OP(OpGenericCastToPtrExplicit);
    CASE_OP(OpBitcast);
    CASE_OP(OpSNegate);
    CASE_OP(OpFNegate);
    CASE_OP(OpIAdd);
    CASE_OP(OpFAdd);
    CASE_OP(OpISub);
    CASE_OP(OpFSub);
    CASE_OP(OpIMul);
    CASE_OP(OpFMul);
    CASE_OP(OpUDiv);
    CASE_OP(OpSDiv);
    CASE_OP(OpFDiv);
    CASE_OP(OpUMod);
    CASE_OP(OpSRem);
    CASE_OP(OpSMod);
    CASE_OP(OpFRem);
    CASE_OP(OpFMod);
    CASE_OP(OpVectorTimesScalar);
    CASE_OP(OpMatrixTimesScalar);
    CASE_OP(OpVectorTimesMatrix);
    CASE_OP(OpMatrixTimesVector);
    CASE_OP(OpMatrixTimesMatrix);
    CASE_OP(OpOuterProduct);
    CASE_OP(OpDot);
    CASE_OP(OpIAddCarry);
    CASE_OP(OpISubBorrow);
    CASE_OP(OpUMulExtended);
    CASE_OP(OpSMulExtended);
    CASE_OP(OpAny);
    CASE_OP(OpAll);
    CASE_OP(OpIsNan);
    CASE_OP(OpIsInf);
    CASE_OP(OpIsFinite);
    CASE_OP(OpIsNormal);
    CASE_OP(OpSignBitSet);
    CASE_OP(OpLessOrGreater);
    CASE_OP(OpOrdered);
    CASE_OP(OpUnordered);
    CASE_OP(OpLogicalEqual);
    CASE_OP(OpLogicalNotEqual);
    CASE_OP(OpLogicalOr);
    CASE_OP(OpLogicalAnd);
    CASE_OP(OpLogicalNot);
    CASE_OP(OpSelect);
    CASE_OP(OpIEqual);
    CASE_OP(OpINotEqual);
    CASE_OP(OpUGreaterThan);
    CASE_OP(OpSGreaterThan);
    CASE_OP(OpUGreaterThanEqual);
    CASE_OP(OpSGreaterThanEqual);
    CASE_OP(OpULessThan);
    CASE_OP(OpSLessThan);
    CASE_OP(OpULessThanEqual);
    CASE_OP(OpSLessThanEqual);
    CASE_OP(OpFOrdEqual);
    CASE_OP(OpFUnordEqual);
    CASE_OP(OpFOrdNotEqual);
    CASE_OP(OpFUnordNotEqual);
    CASE_OP(OpFOrdLessThan);
    CASE_OP(OpFUnordLessThan);
    CASE_OP(OpFOrdGreaterThan);
    CASE_OP(OpFUnordGreaterThan);
    CASE_OP(OpFOrdLessThanEqual);
    CASE_OP(OpFUnordLessThanEqual);
    CASE_OP(OpFOrdGreaterThanEqual);
    CASE_OP(OpFUnordGreaterThanEqual);
    CASE_OP(OpShiftRightLogical);
    CASE_OP(OpShiftRightArithmetic);
    CASE_OP(OpShiftLeftLogical);
    CASE_OP(OpBitwiseOr);
    CASE_OP(OpBitwiseXor);
    CASE_OP(OpBitwiseAnd);
    CASE_OP(OpNot);
    CASE_OP(OpBitFieldInsert);
    CASE_OP(OpBitFieldSExtract);
    CASE_OP(OpBitFieldUExtract);
    CASE_OP(OpBitReverse);
    CASE_OP(OpBitCount);
    CASE_OP(OpDPdx);
    CASE_OP(OpDPdy);
    CASE_OP(OpFwidth);
    CASE_OP(OpDPdxFine);
    CASE_OP(OpDPdyFine);
    CASE_OP(OpFwidthFine);
    CASE_OP(OpDPdxCoarse);
    CASE_OP(OpDPdyCoarse);
    CASE_OP(OpFwidthCoarse);
    CASE_OP(OpEmitVertex);
    CASE_OP(OpEndPrimitive);
    CASE_OP(OpEmitStreamVertex);
    CASE_OP(OpEndStreamPrimitive);
    CASE_OP(OpControlBarrier);
    CASE_OP(OpMemoryBarrier);
    CASE_OP(OpAtomicLoad);
    CASE_OP(OpAtomicStore);
    CASE_OP(OpAtomicExchange);
    CASE_OP(OpAtomicCompareExchange);
    CASE_OP(OpAtomicCompareExchangeWeak);
    CASE_OP(OpAtomicIIncrement);
    CASE_OP(OpAtomicIDecrement);
    CASE_OP(OpAtomicIAdd);
    CASE_OP(OpAtomicISub);
    CASE_OP(OpAtomicSMin);
    CASE_OP(OpAtomicUMin);
    CASE_OP(OpAtomicSMax);
    CASE_OP(OpAtomicUMax);
    CASE_OP(OpAtomicAnd);
    CASE_OP(OpAtomicOr);
    CASE_OP(OpAtomicXor);
    CASE_OP(OpPhi);
    CASE_OP(OpLoopMerge);
    CASE_OP(OpSelectionMerge);
    CASE_OP(OpLabel);
    CASE_OP(OpBranch);
    CASE_OP(OpBranchConditional);
    CASE_OP(OpSwitch);
    CASE_OP(OpKill);
    CASE_OP(OpReturn);
    CASE_OP(OpReturnValue);
    CASE_OP(OpUnreachable);
    CASE_OP(OpLifetimeStart);
    CASE_OP(OpLifetimeStop);
    CASE_OP(OpGroupAsyncCopy);
    CASE_OP(OpGroupWaitEvents);
    CASE_OP(OpGroupAll);
    CASE_OP(OpGroupAny);
    CASE_OP(OpGroupBroadcast);
    CASE_OP(OpGroupIAdd);
    CASE_OP(OpGroupFAdd);
    CASE_OP(OpGroupFMin);
    CASE_OP(OpGroupUMin);
    CASE_OP(OpGroupSMin);
    CASE_OP(OpGroupFMax);
    CASE_OP(OpGroupUMax);
    CASE_OP(OpGroupSMax);
    CASE_OP(OpReadPipe);
    CASE_OP(OpWritePipe);
    CASE_OP(OpReservedReadPipe);
    CASE_OP(OpReservedWritePipe);
    CASE_OP(OpReserveReadPipePackets);
    CASE_OP(OpReserveWritePipePackets);
    CASE_OP(OpCommitReadPipe);
    CASE_OP(OpCommitWritePipe);
    CASE_OP(OpIsValidReserveId);
    CASE_OP(OpGetNumPipePackets);
    CASE_OP(OpGetMaxPipePackets);
    CASE_OP(OpGroupReserveReadPipePackets);
    CASE_OP(OpGroupReserveWritePipePackets);
    CASE_OP(OpGroupCommitReadPipe);
    CASE_OP(OpGroupCommitWritePipe);
    CASE_OP(OpEnqueueMarker);
    CASE_OP(OpEnqueueKernel);
    CASE_OP(OpGetKernelNDrangeSubGroupCount);
    CASE_OP(OpGetKernelNDrangeMaxSubGroupSize);
    CASE_OP(OpGetKernelWorkGroupSize);
    CASE_OP(OpGetKernelPreferredWorkGroupSizeMultiple);
    CASE_OP(OpRetainEvent);
    CASE_OP(OpReleaseEvent);
    CASE_OP(OpCreateUserEvent);
    CASE_OP(OpIsValidEvent);
    CASE_OP(OpSetUserEventStatus);
    CASE_OP(OpCaptureEventProfilingInfo);
    CASE_OP(OpGetDefaultQueue);
    CASE_OP(OpBuildNDRange);
    CASE_OP(OpImageSparseSampleImplicitLod);
    CASE_OP(OpImageSparseSampleExplicitLod);
    CASE_OP(OpImageSparseSampleDrefImplicitLod);
    CASE_OP(OpImageSparseSampleDrefExplicitLod);
    CASE_OP(OpImageSparseSampleProjImplicitLod);
    CASE_OP(OpImageSparseSampleProjExplicitLod);
    CASE_OP(OpImageSparseSampleProjDrefImplicitLod);
    CASE_OP(OpImageSparseSampleProjDrefExplicitLod);
    CASE_OP(OpImageSparseFetch);
    CASE_OP(OpImageSparseGather);
    CASE_OP(OpImageSparseDrefGather);
    CASE_OP(OpImageSparseTexelsResident);
    CASE_OP(OpNoLine);
    CASE_OP(OpAtomicFlagTestAndSet);
    CASE_OP(OpAtomicFlagClear);
    CASE_OP(OpImageSparseRead);
    CASE_OP(OpSizeOf);
    CASE_OP(OpTypePipeStorage);
    CASE_OP(OpConstantPipeStorage);
    CASE_OP(OpCreatePipeFromPipeStorage);
    CASE_OP(OpGetKernelLocalSizeForSubgroupCount);
    CASE_OP(OpGetKernelMaxNumSubgroups);
    CASE_OP(OpTypeNamedBarrier);
    CASE_OP(OpNamedBarrierInitialize);
    CASE_OP(OpMemoryNamedBarrier);
    CASE_OP(OpModuleProcessed);
    CASE_OP(OpExecutionModeId);
    CASE_OP(OpDecorateId);
    CASE_OP(OpGroupNonUniformElect);
    CASE_OP(OpGroupNonUniformAll);
    CASE_OP(OpGroupNonUniformAny);
    CASE_OP(OpGroupNonUniformAllEqual);
    CASE_OP(OpGroupNonUniformBroadcast);
    CASE_OP(OpGroupNonUniformBroadcastFirst);
    CASE_OP(OpGroupNonUniformBallot);
    CASE_OP(OpGroupNonUniformInverseBallot);
    CASE_OP(OpGroupNonUniformBallotBitExtract);
    CASE_OP(OpGroupNonUniformBallotBitCount);
    CASE_OP(OpGroupNonUniformBallotFindLSB);
    CASE_OP(OpGroupNonUniformBallotFindMSB);
    CASE_OP(OpGroupNonUniformShuffle);
    CASE_OP(OpGroupNonUniformShuffleXor);
    CASE_OP(OpGroupNonUniformShuffleUp);
    CASE_OP(OpGroupNonUniformShuffleDown);
    CASE_OP(OpGroupNonUniformIAdd);
    CASE_OP(OpGroupNonUniformFAdd);
    CASE_OP(OpGroupNonUniformIMul);
    CASE_OP(OpGroupNonUniformFMul);
    CASE_OP(OpGroupNonUniformSMin);
    CASE_OP(OpGroupNonUniformUMin);
    CASE_OP(OpGroupNonUniformFMin);
    CASE_OP(OpGroupNonUniformSMax);
    CASE_OP(OpGroupNonUniformUMax);
    CASE_OP(OpGroupNonUniformFMax);
    CASE_OP(OpGroupNonUniformBitwiseAnd);
    CASE_OP(OpGroupNonUniformBitwiseOr);
    CASE_OP(OpGroupNonUniformBitwiseXor);
    CASE_OP(OpGroupNonUniformLogicalAnd);
    CASE_OP(OpGroupNonUniformLogicalOr);
    CASE_OP(OpGroupNonUniformLogicalXor);
    CASE_OP(OpGroupNonUniformQuadBroadcast);
    CASE_OP(OpGroupNonUniformQuadSwap);
    CASE_OP(OpCopyLogical);
    CASE_OP(OpPtrEqual);
    CASE_OP(OpPtrNotEqual);
    CASE_OP(OpPtrDiff);
    CASE_OP(OpColorAttachmentReadEXT);
    CASE_OP(OpDepthAttachmentReadEXT);
    CASE_OP(OpStencilAttachmentReadEXT);
    CASE_OP(OpTerminateInvocation);
    CASE_OP(OpSubgroupBallotKHR);
    CASE_OP(OpSubgroupFirstInvocationKHR);
    CASE_OP(OpSubgroupAllKHR);
    CASE_OP(OpSubgroupAnyKHR);
    CASE_OP(OpSubgroupAllEqualKHR);
    CASE_OP(OpGroupNonUniformRotateKHR);
    CASE_OP(OpSubgroupReadInvocationKHR);
    CASE_OP(OpTraceRayKHR);
    CASE_OP(OpExecuteCallableKHR);
    CASE_OP(OpConvertUToAccelerationStructureKHR);
    CASE_OP(OpIgnoreIntersectionKHR);
    CASE_OP(OpTerminateRayKHR);
    CASE_OP(OpSDot);
    CASE_OP(OpUDot);
    CASE_OP(OpSUDot);
    CASE_OP(OpSDotAccSat);
    CASE_OP(OpUDotAccSat);
    CASE_OP(OpSUDotAccSat);
    CASE_OP(OpTypeCooperativeMatrixKHR);
    CASE_OP(OpCooperativeMatrixLoadKHR);
    CASE_OP(OpCooperativeMatrixStoreKHR);
    CASE_OP(OpCooperativeMatrixMulAddKHR);
    CASE_OP(OpCooperativeMatrixLengthKHR);
    CASE_OP(OpTypeRayQueryKHR);
    CASE_OP(OpRayQueryInitializeKHR);
    CASE_OP(OpRayQueryTerminateKHR);
    CASE_OP(OpRayQueryGenerateIntersectionKHR);
    CASE_OP(OpRayQueryConfirmIntersectionKHR);
    CASE_OP(OpRayQueryProceedKHR);
    CASE_OP(OpRayQueryGetIntersectionTypeKHR);
    CASE_OP(OpImageSampleWeightedQCOM);
    CASE_OP(OpImageBoxFilterQCOM);
    CASE_OP(OpImageBlockMatchSSDQCOM);
    CASE_OP(OpImageBlockMatchSADQCOM);
    CASE_OP(OpGroupIAddNonUniformAMD);
    CASE_OP(OpGroupFAddNonUniformAMD);
    CASE_OP(OpGroupFMinNonUniformAMD);
    CASE_OP(OpGroupUMinNonUniformAMD);
    CASE_OP(OpGroupSMinNonUniformAMD);
    CASE_OP(OpGroupFMaxNonUniformAMD);
    CASE_OP(OpGroupUMaxNonUniformAMD);
    CASE_OP(OpGroupSMaxNonUniformAMD);
    CASE_OP(OpFragmentMaskFetchAMD);
    CASE_OP(OpFragmentFetchAMD);
    CASE_OP(OpReadClockKHR);
    CASE_OP(OpFinalizeNodePayloadsAMDX);
    CASE_OP(OpFinishWritingNodePayloadAMDX);
    CASE_OP(OpInitializeNodePayloadsAMDX);
    CASE_OP(OpGroupNonUniformQuadAllKHR);
    CASE_OP(OpGroupNonUniformQuadAnyKHR);
    CASE_OP(OpHitObjectRecordHitMotionNV);
    CASE_OP(OpHitObjectRecordHitWithIndexMotionNV);
    CASE_OP(OpHitObjectRecordMissMotionNV);
    CASE_OP(OpHitObjectGetWorldToObjectNV);
    CASE_OP(OpHitObjectGetObjectToWorldNV);
    CASE_OP(OpHitObjectGetObjectRayDirectionNV);
    CASE_OP(OpHitObjectGetObjectRayOriginNV);
    CASE_OP(OpHitObjectTraceRayMotionNV);
    CASE_OP(OpHitObjectGetShaderRecordBufferHandleNV);
    CASE_OP(OpHitObjectGetShaderBindingTableRecordIndexNV);
    CASE_OP(OpHitObjectRecordEmptyNV);
    CASE_OP(OpHitObjectTraceRayNV);
    CASE_OP(OpHitObjectRecordHitNV);
    CASE_OP(OpHitObjectRecordHitWithIndexNV);
    CASE_OP(OpHitObjectRecordMissNV);
    CASE_OP(OpHitObjectExecuteShaderNV);
    CASE_OP(OpHitObjectGetCurrentTimeNV);
    CASE_OP(OpHitObjectGetAttributesNV);
    CASE_OP(OpHitObjectGetHitKindNV);
    CASE_OP(OpHitObjectGetPrimitiveIndexNV);
    CASE_OP(OpHitObjectGetGeometryIndexNV);
    CASE_OP(OpHitObjectGetInstanceIdNV);
    CASE_OP(OpHitObjectGetInstanceCustomIndexNV);
    CASE_OP(OpHitObjectGetWorldRayDirectionNV);
    CASE_OP(OpHitObjectGetWorldRayOriginNV);
    CASE_OP(OpHitObjectGetRayTMaxNV);
    CASE_OP(OpHitObjectGetRayTMinNV);
    CASE_OP(OpHitObjectIsEmptyNV);
    CASE_OP(OpHitObjectIsHitNV);
    CASE_OP(OpHitObjectIsMissNV);
    CASE_OP(OpReorderThreadWithHitObjectNV);
    CASE_OP(OpReorderThreadWithHintNV);
    CASE_OP(OpTypeHitObjectNV);
    CASE_OP(OpImageSampleFootprintNV);
    CASE_OP(OpEmitMeshTasksEXT);
    CASE_OP(OpSetMeshOutputsEXT);
    CASE_OP(OpGroupNonUniformPartitionNV);
    CASE_OP(OpWritePackedPrimitiveIndices4x8NV);
    CASE_OP(OpFetchMicroTriangleVertexPositionNV);
    CASE_OP(OpFetchMicroTriangleVertexBarycentricNV);
    CASE_OP(OpReportIntersectionKHR);
    CASE_OP(OpIgnoreIntersectionNV);
    CASE_OP(OpTerminateRayNV);
    CASE_OP(OpTraceNV);
    CASE_OP(OpTraceMotionNV);
    CASE_OP(OpTraceRayMotionNV);
    CASE_OP(OpRayQueryGetIntersectionTriangleVertexPositionsKHR);
    CASE_OP(OpTypeAccelerationStructureKHR);
    CASE_OP(OpExecuteCallableNV);
    CASE_OP(OpTypeCooperativeMatrixNV);
    CASE_OP(OpCooperativeMatrixLoadNV);
    CASE_OP(OpCooperativeMatrixStoreNV);
    CASE_OP(OpCooperativeMatrixMulAddNV);
    CASE_OP(OpCooperativeMatrixLengthNV);
    CASE_OP(OpBeginInvocationInterlockEXT);
    CASE_OP(OpEndInvocationInterlockEXT);
    CASE_OP(OpDemoteToHelperInvocation);
    CASE_OP(OpIsHelperInvocationEXT);
    CASE_OP(OpConvertUToImageNV);
    CASE_OP(OpConvertUToSamplerNV);
    CASE_OP(OpConvertImageToUNV);
    CASE_OP(OpConvertSamplerToUNV);
    CASE_OP(OpConvertUToSampledImageNV);
    CASE_OP(OpConvertSampledImageToUNV);
    CASE_OP(OpSamplerImageAddressingModeNV);
    CASE_OP(OpSubgroupShuffleINTEL);
    CASE_OP(OpSubgroupShuffleDownINTEL);
    CASE_OP(OpSubgroupShuffleUpINTEL);
    CASE_OP(OpSubgroupShuffleXorINTEL);
    CASE_OP(OpSubgroupBlockReadINTEL);
    CASE_OP(OpSubgroupBlockWriteINTEL);
    CASE_OP(OpSubgroupImageBlockReadINTEL);
    CASE_OP(OpSubgroupImageBlockWriteINTEL);
    CASE_OP(OpSubgroupImageMediaBlockReadINTEL);
    CASE_OP(OpSubgroupImageMediaBlockWriteINTEL);
    CASE_OP(OpUCountLeadingZerosINTEL);
    CASE_OP(OpUCountTrailingZerosINTEL);
    CASE_OP(OpAbsISubINTEL);
    CASE_OP(OpAbsUSubINTEL);
    CASE_OP(OpIAddSatINTEL);
    CASE_OP(OpUAddSatINTEL);
    CASE_OP(OpIAverageINTEL);
    CASE_OP(OpUAverageINTEL);
    CASE_OP(OpIAverageRoundedINTEL);
    CASE_OP(OpUAverageRoundedINTEL);
    CASE_OP(OpISubSatINTEL);
    CASE_OP(OpUSubSatINTEL);
    CASE_OP(OpIMul32x16INTEL);
    CASE_OP(OpUMul32x16INTEL);
    CASE_OP(OpConstantFunctionPointerINTEL);
    CASE_OP(OpFunctionPointerCallINTEL);
    CASE_OP(OpAsmTargetINTEL);
    CASE_OP(OpAsmINTEL);
    CASE_OP(OpAsmCallINTEL);
    CASE_OP(OpAtomicFMinEXT);
    CASE_OP(OpAtomicFMaxEXT);
    CASE_OP(OpAssumeTrueKHR);
    CASE_OP(OpExpectKHR);
    CASE_OP(OpDecorateString);
    CASE_OP(OpMemberDecorateString);
    CASE_OP(OpVmeImageINTEL);
    CASE_OP(OpTypeVmeImageINTEL);
    CASE_OP(OpTypeAvcImePayloadINTEL);
    CASE_OP(OpTypeAvcRefPayloadINTEL);
    CASE_OP(OpTypeAvcSicPayloadINTEL);
    CASE_OP(OpTypeAvcMcePayloadINTEL);
    CASE_OP(OpTypeAvcMceResultINTEL);
    CASE_OP(OpTypeAvcImeResultINTEL);
    CASE_OP(OpTypeAvcImeResultSingleReferenceStreamoutINTEL);
    CASE_OP(OpTypeAvcImeResultDualReferenceStreamoutINTEL);
    CASE_OP(OpTypeAvcImeSingleReferenceStreaminINTEL);
    CASE_OP(OpTypeAvcImeDualReferenceStreaminINTEL);
    CASE_OP(OpTypeAvcRefResultINTEL);
    CASE_OP(OpTypeAvcSicResultINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceSetInterBaseMultiReferencePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultInterShapePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceSetInterShapePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultInterDirectionPenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceSetInterDirectionPenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultIntraLumaShapePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultInterMotionVectorCostTableINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultHighPenaltyCostTableINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultMediumPenaltyCostTableINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultLowPenaltyCostTableINTEL);
    CASE_OP(OpSubgroupAvcMceSetMotionVectorCostFunctionINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultIntraLumaModePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultNonDcLumaIntraPenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceGetDefaultIntraChromaModeBasePenaltyINTEL);
    CASE_OP(OpSubgroupAvcMceSetAcOnlyHaarINTEL);
    CASE_OP(OpSubgroupAvcMceSetSourceInterlacedFieldPolarityINTEL);
    CASE_OP(OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL);
    CASE_OP(OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL);
    CASE_OP(OpSubgroupAvcMceConvertToImePayloadINTEL);
    CASE_OP(OpSubgroupAvcMceConvertToImeResultINTEL);
    CASE_OP(OpSubgroupAvcMceConvertToRefPayloadINTEL);
    CASE_OP(OpSubgroupAvcMceConvertToRefResultINTEL);
    CASE_OP(OpSubgroupAvcMceConvertToSicPayloadINTEL);
    CASE_OP(OpSubgroupAvcMceConvertToSicResultINTEL);
    CASE_OP(OpSubgroupAvcMceGetMotionVectorsINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterDistortionsINTEL);
    CASE_OP(OpSubgroupAvcMceGetBestInterDistortionsINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterMajorShapeINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterMinorShapeINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterDirectionsINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterMotionVectorCountINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterReferenceIdsINTEL);
    CASE_OP(OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL);
    CASE_OP(OpSubgroupAvcImeInitializeINTEL);
    CASE_OP(OpSubgroupAvcImeSetSingleReferenceINTEL);
    CASE_OP(OpSubgroupAvcImeSetDualReferenceINTEL);
    CASE_OP(OpSubgroupAvcImeRefWindowSizeINTEL);
    CASE_OP(OpSubgroupAvcImeAdjustRefOffsetINTEL);
    CASE_OP(OpSubgroupAvcImeConvertToMcePayloadINTEL);
    CASE_OP(OpSubgroupAvcImeSetMaxMotionVectorCountINTEL);
    CASE_OP(OpSubgroupAvcImeSetUnidirectionalMixDisableINTEL);
    CASE_OP(OpSubgroupAvcImeSetEarlySearchTerminationThresholdINTEL);
    CASE_OP(OpSubgroupAvcImeSetWeightedSadINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithSingleReferenceINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithDualReferenceINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithDualReferenceStreaminINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithSingleReferenceStreamoutINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithDualReferenceStreamoutINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminoutINTEL);
    CASE_OP(OpSubgroupAvcImeEvaluateWithDualReferenceStreaminoutINTEL);
    CASE_OP(OpSubgroupAvcImeConvertToMceResultINTEL);
    CASE_OP(OpSubgroupAvcImeGetSingleReferenceStreaminINTEL);
    CASE_OP(OpSubgroupAvcImeGetDualReferenceStreaminINTEL);
    CASE_OP(OpSubgroupAvcImeStripSingleReferenceStreamoutINTEL);
    CASE_OP(OpSubgroupAvcImeStripDualReferenceStreamoutINTEL);
    CASE_OP(OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsINTEL);
    CASE_OP(OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINTEL);
    CASE_OP(OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsINTEL);
    CASE_OP(OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINTEL);
    CASE_OP(OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTEL);
    CASE_OP(OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTEL);
    CASE_OP(OpSubgroupAvcImeGetBorderReachedINTEL);
    CASE_OP(OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL);
    CASE_OP(OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL);
    CASE_OP(OpSubgroupAvcImeGetWeightingPatternMinimumMotionVectorINTEL);
    CASE_OP(OpSubgroupAvcImeGetWeightingPatternMinimumDistortionINTEL);
    CASE_OP(OpSubgroupAvcFmeInitializeINTEL);
    CASE_OP(OpSubgroupAvcBmeInitializeINTEL);
    CASE_OP(OpSubgroupAvcRefConvertToMcePayloadINTEL);
    CASE_OP(OpSubgroupAvcRefSetBidirectionalMixDisableINTEL);
    CASE_OP(OpSubgroupAvcRefSetBilinearFilterEnableINTEL);
    CASE_OP(OpSubgroupAvcRefEvaluateWithSingleReferenceINTEL);
    CASE_OP(OpSubgroupAvcRefEvaluateWithDualReferenceINTEL);
    CASE_OP(OpSubgroupAvcRefEvaluateWithMultiReferenceINTEL);
    CASE_OP(OpSubgroupAvcRefEvaluateWithMultiReferenceInterlacedINTEL);
    CASE_OP(OpSubgroupAvcRefConvertToMceResultINTEL);
    CASE_OP(OpSubgroupAvcSicInitializeINTEL);
    CASE_OP(OpSubgroupAvcSicConfigureSkcINTEL);
    CASE_OP(OpSubgroupAvcSicConfigureIpeLumaINTEL);
    CASE_OP(OpSubgroupAvcSicConfigureIpeLumaChromaINTEL);
    CASE_OP(OpSubgroupAvcSicGetMotionVectorMaskINTEL);
    CASE_OP(OpSubgroupAvcSicConvertToMcePayloadINTEL);
    CASE_OP(OpSubgroupAvcSicSetIntraLumaShapePenaltyINTEL);
    CASE_OP(OpSubgroupAvcSicSetIntraLumaModeCostFunctionINTEL);
    CASE_OP(OpSubgroupAvcSicSetIntraChromaModeCostFunctionINTEL);
    CASE_OP(OpSubgroupAvcSicSetBilinearFilterEnableINTEL);
    CASE_OP(OpSubgroupAvcSicSetSkcForwardTransformEnableINTEL);
    CASE_OP(OpSubgroupAvcSicSetBlockBasedRawSkipSadINTEL);
    CASE_OP(OpSubgroupAvcSicEvaluateIpeINTEL);
    CASE_OP(OpSubgroupAvcSicEvaluateWithSingleReferenceINTEL);
    CASE_OP(OpSubgroupAvcSicEvaluateWithDualReferenceINTEL);
    CASE_OP(OpSubgroupAvcSicEvaluateWithMultiReferenceINTEL);
    CASE_OP(OpSubgroupAvcSicEvaluateWithMultiReferenceInterlacedINTEL);
    CASE_OP(OpSubgroupAvcSicConvertToMceResultINTEL);
    CASE_OP(OpSubgroupAvcSicGetIpeLumaShapeINTEL);
    CASE_OP(OpSubgroupAvcSicGetBestIpeLumaDistortionINTEL);
    CASE_OP(OpSubgroupAvcSicGetBestIpeChromaDistortionINTEL);
    CASE_OP(OpSubgroupAvcSicGetPackedIpeLumaModesINTEL);
    CASE_OP(OpSubgroupAvcSicGetIpeChromaModeINTEL);
    CASE_OP(OpSubgroupAvcSicGetPackedSkcLumaCountThresholdINTEL);
    CASE_OP(OpSubgroupAvcSicGetPackedSkcLumaSumThresholdINTEL);
    CASE_OP(OpSubgroupAvcSicGetInterRawSadsINTEL);
    CASE_OP(OpVariableLengthArrayINTEL);
    CASE_OP(OpSaveMemoryINTEL);
    CASE_OP(OpRestoreMemoryINTEL);
    CASE_OP(OpArbitraryFloatSinCosPiINTEL);
    CASE_OP(OpArbitraryFloatCastINTEL);
    CASE_OP(OpArbitraryFloatCastFromIntINTEL);
    CASE_OP(OpArbitraryFloatCastToIntINTEL);
    CASE_OP(OpArbitraryFloatAddINTEL);
    CASE_OP(OpArbitraryFloatSubINTEL);
    CASE_OP(OpArbitraryFloatMulINTEL);
    CASE_OP(OpArbitraryFloatDivINTEL);
    CASE_OP(OpArbitraryFloatGTINTEL);
    CASE_OP(OpArbitraryFloatGEINTEL);
    CASE_OP(OpArbitraryFloatLTINTEL);
    CASE_OP(OpArbitraryFloatLEINTEL);
    CASE_OP(OpArbitraryFloatEQINTEL);
    CASE_OP(OpArbitraryFloatRecipINTEL);
    CASE_OP(OpArbitraryFloatRSqrtINTEL);
    CASE_OP(OpArbitraryFloatCbrtINTEL);
    CASE_OP(OpArbitraryFloatHypotINTEL);
    CASE_OP(OpArbitraryFloatSqrtINTEL);
    CASE_OP(OpArbitraryFloatLogINTEL);
    CASE_OP(OpArbitraryFloatLog2INTEL);
    CASE_OP(OpArbitraryFloatLog10INTEL);
    CASE_OP(OpArbitraryFloatLog1pINTEL);
    CASE_OP(OpArbitraryFloatExpINTEL);
    CASE_OP(OpArbitraryFloatExp2INTEL);
    CASE_OP(OpArbitraryFloatExp10INTEL);
    CASE_OP(OpArbitraryFloatExpm1INTEL);
    CASE_OP(OpArbitraryFloatSinINTEL);
    CASE_OP(OpArbitraryFloatCosINTEL);
    CASE_OP(OpArbitraryFloatSinCosINTEL);
    CASE_OP(OpArbitraryFloatSinPiINTEL);
    CASE_OP(OpArbitraryFloatCosPiINTEL);
    CASE_OP(OpArbitraryFloatASinINTEL);
    CASE_OP(OpArbitraryFloatASinPiINTEL);
    CASE_OP(OpArbitraryFloatACosINTEL);
    CASE_OP(OpArbitraryFloatACosPiINTEL);
    CASE_OP(OpArbitraryFloatATanINTEL);
    CASE_OP(OpArbitraryFloatATanPiINTEL);
    CASE_OP(OpArbitraryFloatATan2INTEL);
    CASE_OP(OpArbitraryFloatPowINTEL);
    CASE_OP(OpArbitraryFloatPowRINTEL);
    CASE_OP(OpArbitraryFloatPowNINTEL);
    CASE_OP(OpLoopControlINTEL);
    CASE_OP(OpAliasDomainDeclINTEL);
    CASE_OP(OpAliasScopeDeclINTEL);
    CASE_OP(OpAliasScopeListDeclINTEL);
    CASE_OP(OpFixedSqrtINTEL);
    CASE_OP(OpFixedRecipINTEL);
    CASE_OP(OpFixedRsqrtINTEL);
    CASE_OP(OpFixedSinINTEL);
    CASE_OP(OpFixedCosINTEL);
    CASE_OP(OpFixedSinCosINTEL);
    CASE_OP(OpFixedSinPiINTEL);
    CASE_OP(OpFixedCosPiINTEL);
    CASE_OP(OpFixedSinCosPiINTEL);
    CASE_OP(OpFixedLogINTEL);
    CASE_OP(OpFixedExpINTEL);
    CASE_OP(OpPtrCastToCrossWorkgroupINTEL);
    CASE_OP(OpCrossWorkgroupCastToPtrINTEL);
    CASE_OP(OpReadPipeBlockingINTEL);
    CASE_OP(OpWritePipeBlockingINTEL);
    CASE_OP(OpFPGARegINTEL);
    CASE_OP(OpRayQueryGetRayTMinKHR);
    CASE_OP(OpRayQueryGetRayFlagsKHR);
    CASE_OP(OpRayQueryGetIntersectionTKHR);
    CASE_OP(OpRayQueryGetIntersectionInstanceCustomIndexKHR);
    CASE_OP(OpRayQueryGetIntersectionInstanceIdKHR);
    CASE_OP(OpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetKHR);
    CASE_OP(OpRayQueryGetIntersectionGeometryIndexKHR);
    CASE_OP(OpRayQueryGetIntersectionPrimitiveIndexKHR);
    CASE_OP(OpRayQueryGetIntersectionBarycentricsKHR);
    CASE_OP(OpRayQueryGetIntersectionFrontFaceKHR);
    CASE_OP(OpRayQueryGetIntersectionCandidateAABBOpaqueKHR);
    CASE_OP(OpRayQueryGetIntersectionObjectRayDirectionKHR);
    CASE_OP(OpRayQueryGetIntersectionObjectRayOriginKHR);
    CASE_OP(OpRayQueryGetWorldRayDirectionKHR);
    CASE_OP(OpRayQueryGetWorldRayOriginKHR);
    CASE_OP(OpRayQueryGetIntersectionObjectToWorldKHR);
    CASE_OP(OpRayQueryGetIntersectionWorldToObjectKHR);
    CASE_OP(OpAtomicFAddEXT);
    CASE_OP(OpTypeBufferSurfaceINTEL);
    CASE_OP(OpTypeStructContinuedINTEL);
    CASE_OP(OpConstantCompositeContinuedINTEL);
    CASE_OP(OpSpecConstantCompositeContinuedINTEL);
    CASE_OP(OpCompositeConstructContinuedINTEL);
    CASE_OP(OpConvertFToBF16INTEL);
    CASE_OP(OpConvertBF16ToFINTEL);
    CASE_OP(OpControlBarrierArriveINTEL);
    CASE_OP(OpControlBarrierWaitINTEL);
    CASE_OP(OpGroupIMulKHR);
    CASE_OP(OpGroupFMulKHR);
    CASE_OP(OpGroupBitwiseAndKHR);
    CASE_OP(OpGroupBitwiseOrKHR);
    CASE_OP(OpGroupBitwiseXorKHR);
    CASE_OP(OpGroupLogicalAndKHR);
    CASE_OP(OpGroupLogicalOrKHR);
    CASE_OP(OpGroupLogicalXorKHR);
    CASE_OP(OpMaskedGatherINTEL);
    CASE_OP(OpMaskedScatterINTEL);
#undef CASE_OP
    default:
        std::stringstream op;
        op << "OpUnknown(" << static_cast<unsigned>(opcode) << ")";
        return op.str();
    }
}

void Spv::Instruction::print() const {
    std::cout << printOpcode(opcode);

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
