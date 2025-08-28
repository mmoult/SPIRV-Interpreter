/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <bit>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define SPV_ENABLE_UTILITY_CODE 1
#include "../../external/spirv.hpp"
module spv.instruction;
import spv.frame;
import spv.token;

bool parseString(const std::vector<uint32_t>& words, unsigned& i, std::stringstream& str) {
    // UTF-8 encoding with four codepoints per word, 0 terminated
    for (; i < words.size(); ++i) {
        uint32_t word = words[i];
        for (unsigned ii = 0; ii < 4; ++ii) {
            char code = static_cast<char>((word >> (ii * 8)) & 0xff);
            if (code == 0) {
                ++i;  // this word was used, queue up the next
                return true;
            }
            str << code;
        }
    }
    return false;  // we reached the end of string before expected (no 0 termination)!
}

void
handle_type(const Token::Type& type, std::vector<Token>& operands, const std::vector<uint32_t>& words, unsigned& i) {
    uint32_t word = words[i++];
    using Type = Token::Type;

    switch (type) {
    default:
        operands.emplace_back(type, word);
        break;
    case Type::INT:
        operands.emplace_back(std::bit_cast<int32_t>(word));
        break;
    case Type::FLOAT:
        operands.emplace_back(std::bit_cast<float>(word));
        break;
    case Type::STRING: {
        std::stringstream ss;
        parseString(words, --i, ss);
        operands.emplace_back(ss.str());
        break;
    }
    }
}

void Instruction::readOp(std::vector<Instruction>& insts, uint16_t opcode, std::vector<uint32_t>& words) noexcept(false
) {
    // Very first, fetch SPIR-V info for the opcode (and also validate it is real)
    bool has_result;
    bool has_type;
    spv::Op op = static_cast<spv::Op>(opcode);
    if (!spv::HasResultAndType(static_cast<spv::Op>(opcode), &has_result, &has_type))
        throw std::invalid_argument("Cannot parse invalid SPIR-V opcode!");

    // Create token operands from the words available and for the given opcode
    using Type = Token::Type;
    // The result and result type will be handled by default (as needed), so do NOT include them in to_load!
    std::vector<Type> to_load;
    std::vector<Type> optional;

    enum Repeat : unsigned {
        NONE = 0,  // Do not repeat values in optional
        WHOLE = 1,  // optional may be present 0 or more times
        LAST = 2,  // the last token in optional may be repeated 0 or more times
    };
    Repeat repeating = Repeat::NONE;

    switch (op) {
    default: {
        // Unsupported op
        std::stringstream err;
        err << "Cannot parse unsupported SPIR-V instruction (" << spv::OpToString(op) << ")!";
        throw std::invalid_argument(err.str());
    }
    case spv::OpNop:  // 1
    case spv::OpUndef:  // 3
    case spv::OpTypeVoid:  // 19
    case spv::OpTypeBool:  // 20
    case spv::OpConstantTrue:  // 41
    case spv::OpConstantFalse:  // 42
    case spv::OpConstantNull:  // 46
    case spv::OpSpecConstantTrue:  // 48
    case spv::OpSpecConstantFalse:  // 49
    case spv::OpFunctionParameter:  // 55
    case spv::OpFunctionEnd:  // 56
    case spv::OpLabel:  // 248
    case spv::OpKill:  // 252
    case spv::OpReturn:  // 253
    case spv::OpUnreachable:  // 255
    case spv::OpNoLine:  // 317
    case spv::OpTerminateInvocation:  // 4416
    case spv::OpIgnoreIntersectionKHR:  // 4448
    case spv::OpTerminateRayKHR:  // 4449
    case spv::OpTypeRayQueryKHR:  // 4472
    case spv::OpTypeAccelerationStructureKHR:  // 5341
        // no operands to handle (besides result and type, if present)
        break;
    case spv::OpSource:  // 3
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::REF);
        optional.push_back(Type::STRING);
        break;
    case spv::OpSourceExtension:  // 4
    case spv::OpExtension:  // 10
        to_load.push_back(Type::STRING);
        break;
    case spv::OpName:  // 5
        to_load.push_back(Type::REF);
        to_load.push_back(Type::STRING);
        break;
    case spv::OpMemberName:  // 6
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::STRING);
        break;
    case spv::OpString:  // 7
    case spv::OpExtInstImport:  // 11
    case spv::OpModuleProcessed:  // 330
        to_load.push_back(Type::STRING);
        break;
    case spv::OpLine:  // 8
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::UINT);
        break;
    case spv::OpExtInst:  // 12
        to_load.push_back(Type::REF);
        [[fallthrough]];
    case spv::OpSpecConstantOp:  // 52
        to_load.push_back(Type::CONST);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpMemoryModel:  // 14
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::CONST);
        break;
    case spv::OpEntryPoint:  // 15
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::STRING);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpExecutionMode:  // 16
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpCapability:  // 17
        to_load.push_back(Type::CONST);
        break;
    case spv::OpTypeInt:  // 21
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::UINT);
        break;
    case spv::OpTypeFloat:  // 22
    case spv::OpConstant:  // 43
    case spv::OpSpecConstant:  // 50
        to_load.push_back(Type::UINT);
        break;
    case spv::OpTypeVector:  // 23
    case spv::OpTypeMatrix:  // 24
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        break;
    case spv::OpTypeImage:  // 25
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        for (unsigned i = 0; i < 4; ++i)
            to_load.push_back(Type::UINT);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::CONST);
        break;
    case spv::OpTypeSampledImage:  // 27
    case spv::OpTypeRuntimeArray:  // 29
    case spv::OpTranspose:  // 84
    case spv::OpImage:  // 100
    case spv::OpConvertFToU:  // 109
    case spv::OpConvertFToS:  // 110
    case spv::OpConvertSToF:  // 111
    case spv::OpConvertUToF:  // 112
    case spv::OpUConvert:  // 113
    case spv::OpSConvert:  // 114
    case spv::OpFConvert:  // 115
    case spv::OpBitcast:  // 124
    case spv::OpSNegate:  // 126
    case spv::OpFNegate:  // 127
    case spv::OpAny:  // 154
    case spv::OpAll:  // 155
    case spv::OpIsNan:  // 156
    case spv::OpIsInf:  // 157
    case spv::OpLogicalNot:  // 168
    case spv::OpNot:  // 200
    case spv::OpBitReverse:  // 204
    case spv::OpBranch:  // 249
    case spv::OpReturnValue:  // 254
    // case spv::OpConvertUToAccelerationStructureKHR: // 4447
    case spv::OpCooperativeMatrixLengthKHR: // 4460
    case spv::OpRayQueryTerminateKHR:  // 4474
    case spv::OpRayQueryConfirmIntersectionKHR:  // 4476
    case spv::OpRayQueryProceedKHR:  // 4477
    case spv::OpRayQueryGetRayTMinKHR:  // 6016
    case spv::OpRayQueryGetRayFlagsKHR:  // 6017
    case spv::OpRayQueryGetIntersectionCandidateAABBOpaqueKHR:  // 6026
    case spv::OpRayQueryGetWorldRayDirectionKHR:  // 6029
    case spv::OpRayQueryGetWorldRayOriginKHR:  // 6030
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeArray:  // 28
    case spv::OpIAdd:  // 128
    case spv::OpFAdd:  // 129
    case spv::OpISub:  // 130
    case spv::OpFSub:  // 131
    case spv::OpIMul:  // 132
    case spv::OpFMul:  // 133
    case spv::OpUDiv:  // 134
    case spv::OpSDiv:  // 135
    case spv::OpFDiv:  // 136
    case spv::OpUMod:  // 137
    case spv::OpSRem:  // 138
    case spv::OpSMod:  // 139
    case spv::OpFRem:  // 140
    case spv::OpFMod:  // 141
    case spv::OpVectorTimesScalar:  // 142
    case spv::OpMatrixTimesScalar:  // 143
    case spv::OpVectorTimesMatrix:  // 144
    case spv::OpMatrixTimesVector:  // 145
    case spv::OpMatrixTimesMatrix:  // 146
    case spv::OpOuterProduct:  // 147
    case spv::OpDot:  // 148
    case spv::OpIAddCarry:  // 149
    case spv::OpISubBorrow:  // 150
    case spv::OpUMulExtended:  // 151
    case spv::OpLogicalEqual:  // 164
    case spv::OpLogicalNotEqual:  // 165
    case spv::OpLogicalOr:  // 166
    case spv::OpLogicalAnd:  // 167
    case spv::OpIEqual:  // 170
    case spv::OpINotEqual:  // 171
    case spv::OpUGreaterThan:  // 172
    case spv::OpSGreaterThan:  // 173
    case spv::OpUGreaterThanEqual:  // 174
    case spv::OpSGreaterThanEqual:  // 175
    case spv::OpULessThan:  // 176
    case spv::OpSLessThan:  // 177
    case spv::OpULessThanEqual:  // 178
    case spv::OpSLessThanEqual:  // 179
    case spv::OpFOrdEqual:  // 180
    case spv::OpFUnordEqual:  // 181
    case spv::OpFOrdNotEqual:  // 182
    case spv::OpFUnordNotEqual:  // 183
    case spv::OpFOrdLessThan:  // 184
    case spv::OpFUnordLessThan:  // 185
    case spv::OpFOrdGreaterThan:  // 186
    case spv::OpFUnordGreaterThan:  // 187
    case spv::OpFOrdLessThanEqual:  // 188
    case spv::OpFUnordLessThanEqual:  // 189
    case spv::OpFOrdGreaterThanEqual:  // 190
    case spv::OpFUnordGreaterThanEqual:  // 191
    case spv::OpShiftRightLogical:  // 194
    case spv::OpShiftRightArithmetic:  // 195
    case spv::OpShiftLeftLogical:  // 196
    case spv::OpBitwiseOr:  // 197
    case spv::OpBitwiseXor:  // 198
    case spv::OpBitwiseAnd:  // 199
    case spv::OpMemoryBarrier:  // 225
    case spv::OpPtrEqual:  // 401
    case spv::OpPtrNotEqual:  // 402
    case spv::OpExecuteCallableKHR:  // 4446
    case spv::OpSDot:  // 4450
    case spv::OpRayQueryGenerateIntersectionKHR:  // 4475
    case spv::OpRayQueryGetIntersectionTypeKHR:  // 4479
    case spv::OpReportIntersectionKHR:  // 5334
    case spv::OpRayQueryGetIntersectionTKHR:  // 6018
    case spv::OpRayQueryGetIntersectionInstanceCustomIndexKHR:  // 6019
    case spv::OpRayQueryGetIntersectionInstanceIdKHR:  // 6020
    case spv::OpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetKHR:  // 6021
    case spv::OpRayQueryGetIntersectionGeometryIndexKHR:  // 6022
    case spv::OpRayQueryGetIntersectionPrimitiveIndexKHR:  // 6023
    case spv::OpRayQueryGetIntersectionBarycentricsKHR:  // 6024
    case spv::OpRayQueryGetIntersectionFrontFaceKHR:  // 6025
    case spv::OpRayQueryGetIntersectionObjectRayDirectionKHR:  // 6027
    case spv::OpRayQueryGetIntersectionObjectRayOriginKHR:  // 6028
    case spv::OpRayQueryGetIntersectionObjectToWorldKHR:  // 6031
    case spv::OpRayQueryGetIntersectionWorldToObjectKHR:  // 6032
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeStruct:  // 30
    case spv::OpTypeFunction:  // 33
    case spv::OpConstantComposite:  // 44
    case spv::OpSpecConstantComposite:  // 51
    case spv::OpFunctionCall:  // 57
    case spv::OpCompositeConstruct:  // 80
        to_load.push_back(Type::REF);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpTypePointer:  // 32
    case spv::OpFunction:  // 54
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeForwardPointer:  // 39
    case spv::OpSelectionMerge:  // 247
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        break;
    case spv::OpVariable:  // 59
        to_load.push_back(Type::CONST);
        optional.push_back(Type::REF);
        break;
    case spv::OpLoad:  // 61
        to_load.push_back(Type::REF);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpStore:  // 62
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::UINT);
        break;
    case spv::OpAccessChain:  // 65
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpDecorate:  // 71
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpMemberDecorate:  // 72
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpVectorShuffle:  // 79
    case spv::OpCompositeInsert:  // 82
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpCompositeExtract:  // 81
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpImageWrite:  // 99
        to_load.push_back(Type::REF);
        [[fallthrough]];
    case spv::OpImageSampleImplicitLod:  // 87
    case spv::OpImageSampleExplicitLod:  // 88
    case spv::OpImageSampleProjImplicitLod:  // 91
    case spv::OpImageSampleProjExplicitLod:  // 92
    case spv::OpImageFetch:  // 95
    case spv::OpImageRead:  // 98
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::CONST);
        optional.push_back(Type::REF);
        repeating = Repeat::LAST;
        break;
    case spv::OpSelect:  // 169
    case spv::OpBitFieldSExtract:  // 202
    case spv::OpBitFieldUExtract:  // 203
    case spv::OpControlBarrier:  // 224
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpBitFieldInsert:  // 201
    case spv::OpAtomicIAdd:  // 234
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpPhi:  // 245
        to_load.push_back(Type::REF);  // value
        to_load.push_back(Type::REF);  // block
        optional.push_back(Type::REF);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpLoopMerge:  // 246
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpBranchConditional:  // 250
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        break;
    case spv::OpSwitch:  // 251
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::INT);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpExecutionModeId:  // 331
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::REF);
        repeating = Repeat::WHOLE;
        break;
    case spv::OpTraceRayKHR:  // 4445
        for (int i = 0; i < 11; ++i)
            to_load.push_back(Type::REF);
        break;
    case spv::OpTypeCooperativeMatrixKHR:  // 4456
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpCooperativeMatrixLoadKHR:  // 4457
        to_load.push_back(Type::REF);  // Pointer
        to_load.push_back(Type::REF);  // Memory Layout
        optional.push_back(Type::REF);  // Stride
        optional.push_back(Type::UINT);  // Memory Operand
        for (int i = 6; i < words.size(); i++)
            optional.push_back(Type::UINT);
        // repeating = Repeat::WHOLE;
        break;
    case spv::OpCooperativeMatrixStoreKHR:  // 4458
        to_load.push_back(Type::REF);  // Pointer
        to_load.push_back(Type::REF);  // Object
        to_load.push_back(Type::REF);  // Memory Layout
        optional.push_back(Type::REF);  // Stride
        optional.push_back(Type::UINT);  // Memory Operand
        for (int i = 5; i < words.size(); i++)
            optional.push_back(Type::UINT);
        break;
    case spv::OpCooperativeMatrixMulAddKHR: // 4459
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::CONST);
        break;
    case spv::OpRayQueryInitializeKHR:  // 4473
        for (int i = 0; i < 8; ++i)
            to_load.push_back(Type::REF);
        break;
    }

    Instruction& inst = insts.emplace_back(op, has_result, has_type);
    // Create tokens as requested
    unsigned i = 0;

    auto check_limit = [&](const std::string& parse_what) {
        if (i >= words.size()) {
            std::stringstream err;
            err << "Missing words while parsing " << parse_what << "instruction " << spv::OpToString(op) << "!";
            throw std::length_error(err.str());
        }
    };

    // If the op has a result type, that comes first
    if (has_type) {
        check_limit("result type of ");
        inst.operands.emplace_back(Type::REF, words[i++]);
    }
    // Then the result comes next
    if (has_result) {
        check_limit("result of ");
        inst.operands.emplace_back(Type::REF, words[i++]);
    }

    for (const auto& type : to_load) {
        check_limit("");
        handle_type(type, inst.operands, words, i);
    }

    if (!optional.empty()) {
        // Try optional.
        // The list may be repeated if "repeating" is not NONE
        do {
            // Can safely break out of a check if at the beginning of an optional iteration
            if (i >= words.size())
                break;

            for (unsigned j = 0; j < optional.size(); ++j) {
                auto opt_type = optional[j];

                // For WHOLE repeating mode, if any in optional in present, all in the list must exist
                // For LAST repeating mode, the last token is independent- appearing multiple times or not at all
                if ((j == (optional.size() - 1)) && (repeating == Repeat::LAST)) {
                    while (i < words.size())
                        handle_type(opt_type, inst.operands, words, i);
                    // Done
                    repeating = Repeat::NONE;
                } else {
                    check_limit("");
                    handle_type(opt_type, inst.operands, words, i);
                }
            }
        } while (repeating > 0);
    }

    // Verify that there are no extra words
    if (i < words.size()) {
        std::stringstream err;
        err << "Extra words while parsing instruction " << spv::OpToString(op) << "!";
        throw std::length_error(err.str());
    }
}
