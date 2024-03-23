/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
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

    Value* dst_val = nullptr;
    switch (opcode) {
    default:
        // fall back on the makeResult function (no fallback should use location!)
        if (!makeResult(data, 0, nullptr))
            throw std::runtime_error("Unsupported instruction execution!");
        // If the instruction did make a result, success! These instructions cannot modify control flow,
        // so assume inc_pc = true
        break;
    case spv::OpFunction: // 54
    case spv::OpLabel: // 248
        break;  // should print for verbose
    case spv::OpFunctionEnd: // 56
        throw std::runtime_error("Missing return before function end!");
    case spv::OpVariable: // 59
        // Since this instruction should be run statically, we can assume the variable already exists
        // All we need to do here is set the default value (in case not set before)
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
    case spv::OpBranch: { // 249
        Value* dst = getValue(0, data);
        Primitive* dst2 = static_cast<Primitive*>(dst);
        frame.setPC(dst2->data.u32);
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
    }

    if (dst_val != nullptr) {
        assert(operands[1].type == Token::Type::REF);
        auto result_at = std::get<unsigned>(operands[1].raw);
        data[result_at].redefine(dst_val);
    }

    if (inc_pc)
        frame_stack.back().incPC();
}
