/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <catch2/catch_test_macros.hpp>
#include "../../src/util/compare.hpp"

TEST_CASE("eq_float", "[compare]") {
    CHECK(Compare::eq_float(0.0f, 0.0f, 6)); // trivial eq
    CHECK(Compare::eq_float(-0.0004f, 0.f, 4)); // negative to positive, round down
    CHECK(!Compare::eq_float(3.4567f, 3.4564f, 4));
}

TEST_CASE("nbound", "[compare]") {
    SECTION("min") {
        // Same sign (pos)
        CHECK(Compare::nbound(true, 1.0f, 2.0f) == 1.0f);
        CHECK(Compare::nbound(true, 2.0f, 1.0f) == 1.0f);
        // Same sign (neg)
        CHECK(Compare::nbound(true, -1.0f, -2.0f) == -2.0f);
        CHECK(Compare::nbound(true, -2.0f, -1.0f) == -2.0f);
        // Diff sign
        CHECK(Compare::nbound(true, -1.0f, 2.0f) == -1.0f);
        CHECK(Compare::nbound(true, 2.0f, -1.0f) == -1.0f);
        CHECK(Compare::nbound(true, 1.0f, -2.0f) == -2.0f);
        CHECK(Compare::nbound(true, -2.0f, 1.0f) == -2.0f);
        // NaN
        CHECK(Compare::nbound(true, -3.0f, NAN) == -3.0f);
        CHECK(Compare::nbound(true, NAN, -3.0f) == -3.0f);
        CHECK(Compare::nbound(true, 3.0f, NAN) == 3.0f);
        CHECK(Compare::nbound(true, NAN, 3.0f) == 3.0f);
        // Zeros
        CHECK(std::signbit(Compare::nbound(true, -0.0f, 0.0f)));
        CHECK(std::signbit(Compare::nbound(true, 0.0f, -0.0f)));
    }

    SECTION("max") {
        // Same sign (pos)
        CHECK(Compare::nbound(false, 1.0f, 2.0f) == 2.0f);
        CHECK(Compare::nbound(false, 2.0f, 1.0f) == 2.0f);
        // Same sign (neg)
        CHECK(Compare::nbound(false, -1.0f, -2.0f) == -1.0f);
        CHECK(Compare::nbound(false, -2.0f, -1.0f) == -1.0f);
        // Diff sign
        CHECK(Compare::nbound(false, -1.0f, 2.0f) == 2.0f);
        CHECK(Compare::nbound(false, 2.0f, -1.0f) == 2.0f);
        CHECK(Compare::nbound(false, 1.0f, -2.0f) == 1.0f);
        CHECK(Compare::nbound(false, -2.0f, 1.0f) == 1.0f);
        // NaN
        CHECK(Compare::nbound(false, -3.0f, NAN) == -3.0f);
        CHECK(Compare::nbound(false, NAN, -3.0f) == -3.0f);
        CHECK(Compare::nbound(false, 3.0f, NAN) == 3.0f);
        CHECK(Compare::nbound(false, NAN, 3.0f) == 3.0f);
        // Zeros
        CHECK(!std::signbit(Compare::nbound(false, -0.0f, 0.0f)));
        CHECK(!std::signbit(Compare::nbound(false, 0.0f, -0.0f)));
    }
}
