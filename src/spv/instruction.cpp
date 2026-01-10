/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "instruction.hpp"

#include <sstream>
#include <stdexcept>

Instruction::Extension Instruction::extensionFromString(const std::string& ext_name) const {
    // Contains only implemented extensions
    static const std::vector<std::string> supported_ext {
        "GLSL.std.450",
        "SPV_KHR_ray_tracing",
        "SPV_KHR_ray_query",
        "NonSemantic.Shader.DebugInfo.100",
        "NonSemantic.DebugPrintf"
    };

    for (unsigned i = 0; i < supported_ext.size(); ++i) {
        if (ext_name == supported_ext[i])
            return static_cast<Instruction::Extension>(i);
    }
    return Instruction::Extension::INVALID;
}

unsigned Instruction::checkRef(unsigned idx, unsigned len) const noexcept(false) {
    assert(idx < operands.size());
    assert(operands[idx].type == Token::Type::REF);
    auto result_at = std::get<unsigned>(operands[idx].raw);
    if (result_at >= len) {
        std::stringstream err;
        err << "Reference found (" << result_at << ") beyond data bound (" << len << ")!";
        throw new std::runtime_error(err.str());
    }
    return result_at;
}

Value* Instruction::getHeadValue(const Pointer& pointer, DataView& data) const noexcept(false) {
    unsigned start = pointer.getHead();
    // Head can be either a variable or a value
    Value* head = data[start].getValue();
    if (head == nullptr) {
        Variable* head_var = data[start].getVariable();
        if (head_var == nullptr)
            throw std::runtime_error("Pointer head is neither a variable or a value!");
        head = &head_var->getVal();
    }
    // In some cases (especially common with HLSL), the head can itself be a pointer. If so, dereference that
    // pointer to get a simple head value:
    if (head != nullptr && head->getType().getBase() == DataType::POINTER) {
        Pointer& pointer = *static_cast<Pointer*>(head);
        head = getHeadValue(pointer, data);
        return pointer.dereference(*head);
    }
    return head;
}

Value* Instruction::getFromPointer(unsigned index, DataView& data) const noexcept(false) {
    if (Variable* from = getVariable(index, data); from != nullptr)
        return &from->getVal();

    Value* dst_ptr = getValue(index, data);
    if (dst_ptr == nullptr || dst_ptr->getType().getBase() != DataType::POINTER)
        throw std::runtime_error("Need either a variable or pointer!");

    Pointer& pointer = *static_cast<Pointer*>(dst_ptr);
    Value* head = getHeadValue(pointer, data);
    return pointer.dereference(*head);
}

void Instruction::ioGen(
    DataView& data,
    std::vector<unsigned>& ins,
    std::vector<unsigned>& outs,
    std::vector<unsigned>& specs,
    ValueMap& provided,
    const Instruction& entry_point
) const noexcept(false) {
    switch (opcode) {
    case spv::OpSpecConstantTrue:
    case spv::OpSpecConstantFalse:
    case spv::OpSpecConstant:
    case spv::OpSpecConstantComposite:
    case spv::OpVariable:
        break;
    default:
        return;
    }

    Variable& var = *getVariable(1, data);
    unsigned id = std::get<unsigned>(operands[1].raw);

    using SC = spv::StorageClass;
    switch (var.getStorageClass()) {
    case SC::StorageClassPushConstant:
        if (var.isSpecConst()) {
            // Try to find this's value in the map. If not present, we keep the original value.
            std::string name = var.getName();
            if (provided.contains(name))
                var.getVal().copyFrom(*provided[name]);
            specs.push_back(id);
            break;
        }
        ins.push_back(id);
        break;
    case SC::StorageClassUniformConstant:
        // If the type is an image, then it may have been written to
        // TODO: for a more complete solution, we may need to recursively search the type for any images.
        if (var.getVal().getType().getBase() == DataType::IMAGE && var.isWritable())
            outs.push_back(id);
        ins.push_back(id);
        break;
    case SC::StorageClassInput:
    case SC::StorageClassShaderRecordBufferKHR:
        ins.push_back(id);
        break;
    case SC::StorageClassUniform:
        ins.push_back(id);
        // Uniforms decorated with Bufferblock was the pre-SPIR-V 1.3 solution for what is now `StorageBuffer`.
        if (var.getVal().getType().isBufferBlock() && var.isWritable())
            outs.push_back(id);
        break;
    case SC::StorageClassCrossWorkgroup:
    case SC::StorageClassStorageBuffer:
    case SC::StorageClassIncomingCallableDataKHR:
    case SC::StorageClassIncomingRayPayloadKHR:
        ins.push_back(id);
        if (var.isWritable())
            outs.push_back(id);
        break;
    case SC::StorageClassOutput:
    case SC::StorageClassCallableDataKHR:
    case SC::StorageClassRayPayloadKHR:
        outs.push_back(id);
        break;
    case SC::StorageClassHitAttributeKHR:
        switch (entry_point.getShaderStage()) {
        default:
            throw std::runtime_error("Bad execution model using storage class HitAttributeKHR.");
        case spv::ExecutionModelIntersectionKHR:
            ins.push_back(id);
            outs.push_back(id);
            break;
        case spv::ExecutionModelAnyHitKHR:
        case spv::ExecutionModelClosestHitKHR:
            ins.push_back(id);
            break;
        }
        break;
    case SC::StorageClassPrivate:
    case SC::StorageClassFunction:
    case SC::StorageClassWorkgroup:
    default:
        // these aren't used for public interfaces
        break;
    }
}

const EntryPoint& Instruction::getEntryPoint(DataView& data) const noexcept(false) {
    assert(opcode == spv::OpEntryPoint);

    // The entry function ref is operand 1
    const EntryPoint* fx = getEntryPoint(1, data);
    if (fx == nullptr)
        throw std::runtime_error("Missing entry function in entry declaration!");
    return *fx;
}
EntryPoint& Instruction::getEntryPoint(DataView& data) {
    assert(opcode == spv::OpEntryPoint);

    // The entry function ref is operand 1
    EntryPoint* fx = getEntryPoint(1, data);
    if (fx == nullptr)
        throw std::runtime_error("Missing entry function in entry declaration!");
    return *fx;
}

bool Instruction::queueDecoration(unsigned data_size, unsigned location, DecoQueue& queue) const {
    // If instruction is a decoration, queue it
    switch (opcode) {
    default:
        return false;
    case spv::OpName:  // 5
    case spv::OpMemberName:  // 6
    case spv::OpEntryPoint:  // 15
    case spv::OpExecutionMode:  // 16
    case spv::OpDecorate:  // 71
    case spv::OpMemberDecorate:  // 72
    case spv::OpExecutionModeId:  // 331
        unsigned to_decor = checkRef((opcode == spv::OpEntryPoint) ? 1 : 0, data_size);
        // Search through the queue to see if the ref already has a request
        unsigned i = 0;
        for (; i < queue.size(); ++i) {
            if (queue[i].toDecorate == to_decor)
                break;
        }
        if (i == queue.size()) {
            // The request was not found
            queue.emplace_back(to_decor);
        }
        queue[i].pending.push_back(location);
        break;
    }
    return true;
}
