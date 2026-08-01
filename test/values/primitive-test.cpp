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
