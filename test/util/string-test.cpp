/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
import util.string;

std::string to_string(float x) {
    std::stringstream s;
    print_float(s, x);
    auto str = s.str();  // copied for debugging purposes
    return str;
}

TEST_CASE("print_float", "[string]") {
#define CHECK_F(fl) CHECK(to_string(fl) == #fl)
    CHECK_F(32.5);
    CHECK_F(0.0);
    CHECK_F(-0.0);
    CHECK_F(1.0);
    CHECK_F(-22346490.0);
    CHECK_F(0.000123345);
    CHECK_F(-4.2829E-7);
    CHECK_F(1000.0);
    CHECK_F(0.01);
}
