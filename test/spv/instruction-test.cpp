/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <catch2/catch_test_macros.hpp>
#include "../../external/GLSL.std.450.h"
#include "../../src/spv/instruction.hpp"
#include "../../src/spv/data/manager.hpp"
#include "../../src/util/compare.hpp"
#include "../../src/values/aggregate.hpp"

struct DummyInstruction : public Instruction {
    DummyInstruction(spv::Op op, bool has_result, bool has_type)
        : Instruction(op, has_result, has_type) {}

    static DummyInstruction make(spv::Op op) {
        bool has_result;
        bool has_type;
        assert(spv::HasResultAndType(op, &has_result, &has_type));
        return DummyInstruction(op, has_result, has_type);
    }

    void setOperand(unsigned idx, const Token& token) {
        while (idx >= operands.size())
            operands.emplace_back(0);
        operands[idx] = token;
    }
};

template<typename T>
Token make_ref(DataView& data, T* val) {
    unsigned idx = data.getSource()->allocateNew();
    data[idx].redefine(Data(val));
    return Token(Token::Type::REF, idx);
}

Array* construct_vec4(double x, double y, double z, double w) {
    std::vector<Value*> elements{
        new Primitive(x),
        new Primitive(y),
        new Primitive(z),
        new Primitive(w),
    };
    return new Array(elements);
}

void check_vec4(const Array& vec, double x, double y, double z, double w, bool debug) {
    REQUIRE(vec.getSize() == 4);
    // Print the vector for debugging
    if (debug) {
        std::cout << "[";
        for (size_t i = 0; i < vec.getSize(); ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << static_cast<const Primitive*>(vec[i])->data.f;
        }
        std::cout << "]" << std::endl;
    }
    constexpr unsigned sigfigs = 15;
    CHECK(Compare::eq_float(static_cast<const Primitive*>(vec[0])->data.f, x, sigfigs));
    CHECK(Compare::eq_float(static_cast<const Primitive*>(vec[1])->data.f, y, sigfigs));
    CHECK(Compare::eq_float(static_cast<const Primitive*>(vec[2])->data.f, z, sigfigs));
    CHECK(Compare::eq_float(static_cast<const Primitive*>(vec[3])->data.f, w, sigfigs));
}

TEST_CASE("OpVectorTimesMatrix", "[instruction]") {
    DataManager manager;
    DataView& data = *manager.makeView();

    auto inst = DummyInstruction::make(spv::OpVectorTimesMatrix);
    auto result_id = manager.allocateNew();
    auto* fp64 = new Type(Type::primitive(DataType::FLOAT, 64));
    auto* fvec4 = new Type(Type::array(4, *fp64));
    auto* vector = construct_vec4(1.0, 2.0, 3.0, 4.0);
    std::vector<Value*> mat_elements{
        construct_vec4(-1.0, 1.5, -2.0, 2.5),
        construct_vec4(-2.0, 2.5, -3.0, 3.5),
        construct_vec4(-3.0, 3.5, -4.0, 4.5),
        construct_vec4(-4.0, 4.5, -5.0, 5.5),
    };
    auto* matrix = new Array(mat_elements);

    inst.setOperand(0, make_ref(data, fvec4));
    inst.setOperand(1, Token(Token::Type::REF, result_id));
    inst.setOperand(2, make_ref(data, vector)); // vector_id
    inst.setOperand(3, make_ref(data, matrix)); // matrix_id

    REQUIRE(inst.makeResult(data, 0, nullptr));
    // Check the result created
    const auto& result = static_cast<const Array&>(*data[result_id].getValue());
    check_vec4(result, 6.0, 8.0, 10.0, 12.0, false);
}

TEST_CASE("OpMatrixTimesVector", "[instruction]") {
    DataManager manager;
    DataView& data = *manager.makeView();

    auto inst = DummyInstruction::make(spv::OpMatrixTimesVector);
    auto result_id = manager.allocateNew();
    auto* fp64 = new Type(Type::primitive(DataType::FLOAT, 64));
    auto* fvec4 = new Type(Type::array(4, *fp64));
    std::vector<Value*> mat_elements{
        construct_vec4(1.1, -1.2, 1.3, -1.4),
        construct_vec4(-1.2, 1.3, -1.4, 1.5),
        construct_vec4(1.3, -1.4, 1.5, -1.6),
        construct_vec4(-1.4, 1.5, -1.6, 1.7),
    };
    auto* matrix = new Array(mat_elements);
    auto* vector = construct_vec4(0.5, 0.6, 0.7, 0.8);

    inst.setOperand(0, make_ref(data, fvec4));
    inst.setOperand(1, Token(Token::Type::REF, result_id));
    inst.setOperand(2, make_ref(data, matrix)); // matrix_id
    inst.setOperand(3, make_ref(data, vector)); // vector_id

    REQUIRE(inst.makeResult(data, 0, nullptr));
    // Check the result created
    const auto& result = static_cast<const Array&>(*data[result_id].getValue());
    check_vec4(result, -0.38, 0.4, -0.42, 0.44, false);
}

TEST_CASE("GLSLstd450PackHalf2x16", "[instruction]") {
    DataManager manager;
    DataView& data = *manager.makeView();

    auto inst = DummyInstruction::make(spv::OpExtInst);
    auto result_id = manager.allocateNew();
    auto* u32 = new Type(Type::primitive(DataType::UINT, 32));

    inst.setOperand(0, make_ref(data, u32));
    inst.setOperand(1, Token(Token::Type::REF, result_id));
    inst.setOperand(2, make_ref(data, new Primitive(static_cast<uint64_t>(Extension::GLSL_STD_450))));
    inst.setOperand(3, Token(Token::Type::CONST, GLSLstd450PackHalf2x16)); // ext opcode

    auto check_pack = [&](double lo, double hi, uint32_t expected) {
        std::vector<Value*> elements{
            new Primitive(lo),
            new Primitive(hi),
        };
        Array* vector = new Array(elements);
        inst.setOperand(4, make_ref(data, vector));

        REQUIRE(inst.makeResult(data, 0, nullptr));
        // Check the result created
        const auto& result = static_cast<const Primitive&>(*data[result_id].getValue());
        REQUIRE(result.data.u == expected);
    };

    SECTION("typical") {
        // 1.5 = 0x3E00
        // 6.8 = 0x46CD
        // => 0x46CD3E00 = 1187855872
        check_pack(1.5, 6.8, 1187855872);
    }

    SECTION("negatives") {
        // -8.2 = 0xC81A
        // -10.3 = 0xC926
        // => 0xC926C81A = 3374762010
        check_pack(-8.2, -10.3, 3374762010);
    }

    SECTION("large") {
        // 65504 = 0x7BFF
        // -65504 = 0xFBFF
        // => 0xFBFF7BFF = 4227824639
        check_pack(65504.0, -65504.0, 4227824639);
    }
}
