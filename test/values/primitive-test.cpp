/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "../../src/util/bits.hpp"
#include "../../src/values/primitive.hpp"
#include "../../src/values/type.hpp"

// A bool has only two valid object representations, and compilers optimize on that. Primitive stores bools in a union
// alongside 64-bit members, so every write of `b` has to leave the raw bits within {0, 1} -- otherwise later reads of
// `data.b` are undefined and reads of `data.all` see stale bytes above the bool.
TEST_CASE("A bool Primitive never holds an invalid representation", "[primitive]") {
    SECTION("dummy-initialized") {
        // This used to set data.u = 0x7890ABCD, leaving data.b holding 0xCD.
        Primitive p(Type::primitive(DataType::BOOL), true);
        CHECK(p.data.all <= 1);
        CHECK(p.data.b == false);
        CHECK(p.getRaw() <= 1);
    }

    SECTION("null-initialized") {
        Primitive p(Type::primitive(DataType::BOOL), false);
        CHECK(p.data.all == 0);
        CHECK(p.data.b == false);
    }

    SECTION("from a bool") {
        Primitive t(true);
        CHECK(t.data.all == 1);
        Primitive f(false);
        CHECK(f.data.all == 0);
    }

    SECTION("copyFrom a uint leaves no stale high bytes") {
        // Start from a dummy uint so the raw bits are non-zero, then convert to bool.
        Primitive p(Type::primitive(DataType::UINT, 64), true);
        REQUIRE(p.data.all != 0);
        Primitive as_bool(Type::primitive(DataType::BOOL), false);
        as_bool.copyFrom(p);
        CHECK(as_bool.data.all <= 1);
        CHECK(as_bool.data.b == true);  // the dummy uint is nonzero
    }
}

// uSub builds an artificial borrow bit at `1 << prec`. With `1` as an int this was undefined for prec >= 32 and
// silently produced the wrong borrow for a 32-bit subtraction.
TEST_CASE("Primitive::uSub borrows correctly at every precision", "[primitive]") {
    auto sub = [](unsigned prec, uint64_t a, uint64_t b) {
        Primitive lhs(a, prec);
        Primitive rhs(b, prec);
        Primitive diff(Type::primitive(DataType::UINT, prec), false);
        const bool borrow = lhs.uSub(&rhs, &diff);
        return std::pair {diff.data.u, borrow};
    };

    for (unsigned prec : {8u, 16u, 32u, 64u}) {
        const uint64_t mask = Bits::ones<uint64_t>(prec);

        // No borrow: 5 - 3 == 2
        auto [d1, b1] = sub(prec, 5, 3);
        CHECK(d1 == 2);
        CHECK(b1 == false);

        // Exact zero: 7 - 7 == 0, no borrow
        auto [d2, b2] = sub(prec, 7, 7);
        CHECK(d2 == 0);
        CHECK(b2 == false);

        // Borrow: 0 - 1 wraps to all ones for the precision
        auto [d3, b3] = sub(prec, 0, 1);
        CHECK(d3 == mask);
        CHECK(b3 == true);

        // Borrow: 3 - 5 wraps to mask - 1
        auto [d4, b4] = sub(prec, 3, 5);
        CHECK(d4 == ((3 - 5) & mask));
        CHECK(b4 == true);
    }
}

TEST_CASE("Primitive::uAdd carries correctly at every precision", "[primitive]") {
    auto add = [](unsigned prec, uint64_t a, uint64_t b) {
        Primitive lhs(a, prec);
        Primitive rhs(b, prec);
        Primitive sum(Type::primitive(DataType::UINT, prec), false);
        const bool carry = lhs.uAdd(&rhs, &sum);
        return std::pair {sum.data.u, carry};
    };

    for (unsigned prec : {8u, 16u, 32u, 64u}) {
        const uint64_t mask = Bits::ones<uint64_t>(prec);

        auto [s1, c1] = add(prec, 2, 3);
        CHECK(s1 == 5);
        CHECK(c1 == false);

        // mask + 1 wraps to 0 and carries
        auto [s2, c2] = add(prec, mask, 1);
        CHECK(s2 == 0);
        CHECK(c2 == true);
    }
}
