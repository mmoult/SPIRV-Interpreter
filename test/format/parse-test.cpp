/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cstdint>
#include <limits>
#include <string>
#include <sstream>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "../../src/values/value.hpp"
import format.parse;
import value.aggregate;
import value.primitive;

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
