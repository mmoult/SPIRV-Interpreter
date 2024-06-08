/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <catch2/catch_test_macros.hpp>
import util.compare;

TEST_CASE("eq_float", "[compare]") {
    CHECK(Compare::eq_float(0.0f, 0.0f, 6)); // trivial eq
    CHECK(Compare::eq_float(-0.0004f, 0.f, 4)); // negative to positive, round down
    CHECK(!Compare::eq_float(3.4567f, 3.4564f, 4));
}
