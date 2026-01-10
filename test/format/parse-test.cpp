/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cstdint>
#include <limits>
#include <string>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include "../../src/values/value.hpp"
#include "../../src/format/parse.hpp"
#include "../../src/values/aggregate.hpp"
#include "../../src/values/primitive.hpp"

class TestFormat : public ValueFormat {
private:
    void unused() {
        // Unused for this testing subclass. Trigger assert to show that this code is never executed
        assert(false);
    }

protected:
    /// @brief Give the derived class an opportunity to handle special floats while parsing a number
    /// @param handle the handle to read from
    /// @return one of none, infinity, or NaN
    SpecialFloatResult isSpecialFloat(LineHandler& handler) const override {
        if (handler.matchId("inf"))
            return SpecialFloatResult::F_INF;
        if (handler.matchId("nan"))
            return SpecialFloatResult::F_NAN;
        return SpecialFloatResult::F_NONE;
    }

    /// @brief Parse and return a single key-value pair
    /// @param vars variables to save to- a map of names to values
    /// @param handle a handler to parse the value from
    std::tuple<std::string, Value*> parseVariable(LineHandler& handle) override {
        unused();
        return {};
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override {
        unused();
    }

    void verifyBlank(LineHandler& handle) override {
        unused();
    }

public:
    using ValueFormat::parseNumber;
    using ValueFormat::LineHandler;
    using ValueFormat::constructArrayFrom;

    void printFile(std::stringstream& out, const ValueMap& vars) override {
        unused();
    }
};

[[nodiscard]] Primitive* parse_num(std::string line, const TestFormat& format) {
    TestFormat::LineHandler handle(&line, nullptr);  // no file in this case
    return static_cast<Primitive*>(format.parseNumber(handle));
}

// I am not a fan of the redundancy either, but inlining it makes it MUCH easier to debug failures.
void check_float(TestFormat& format, float val) {
    std::stringstream tos;
    tos << val;
    std::string str = tos.str();
    Primitive* prim = parse_num(str, format);
    float got = prim->data.fp32;
    auto type = prim->getType().getBase();
    delete prim;
    REQUIRE(got == val);
    REQUIRE(type == DataType::FLOAT);
}
void check_int(TestFormat& format, int val) {
    std::stringstream tos;
    tos << val;
    std::string str = tos.str();
    Primitive* prim = parse_num(str, format);
    float got = prim->data.i32;
    auto type = prim->getType().getBase();
    delete prim;
    REQUIRE(got == val);
    REQUIRE(type == DataType::INT);
}
void check_uint(TestFormat& format, int val) {
    std::stringstream tos;
    tos << val;
    std::string str = tos.str();
    Primitive* prim = parse_num(str, format);
    float got = prim->data.u32;
    auto type = prim->getType().getBase();
    delete prim;
    REQUIRE(got == val);
    REQUIRE(type == DataType::UINT);
}

TEST_CASE("parseNumber", "[parse]") {
    TestFormat format;

    check_float(format, 0.5);
    check_float(format, 1.2);
    check_float(format, 0.12346);
    check_float(format, -35.482);
    check_uint(format, 0);
    check_uint(format, std::numeric_limits<int32_t>::max());
    check_uint(format, std::numeric_limits<uint32_t>::min());
    check_int(format, -1);
    check_int(format, std::numeric_limits<int32_t>::min());
}

TEST_CASE("construction", "[parse]") {
    TestFormat format;

    SECTION("array") {
        std::vector<const Value*> elements;
        elements.reserve(3);
        // Do a difficult order: uint, int, float
        // There will be a different union result after each index
        Primitive ui(2u);
        Primitive si(-5);
        Primitive fl(1.5f);
        elements.push_back(&ui);
        elements.push_back(&si);
        elements.push_back(&fl);
        Value* val = format.constructArrayFrom(elements);
        REQUIRE(val->getType().getBase() == DataType::ARRAY);
        REQUIRE(val->getType().getElement().getBase() == DataType::FLOAT);
        Array& arr = static_cast<Array&>(*val);
        REQUIRE(static_cast<Primitive&>(*arr[0]).data.fp32 == 2.0f);
        REQUIRE(static_cast<Primitive&>(*arr[1]).data.fp32 == -5.0f);
        REQUIRE(static_cast<Primitive&>(*arr[2]).data.fp32 == 1.5f);
        delete val;
    }
}
