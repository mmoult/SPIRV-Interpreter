#include <catch2/catch_test_macros.hpp>

import util;

TEST_CASE("eq_float", "[util]") {
    REQUIRE(Util::eq_float(0.0f, 0.0f, 6)); // trivial eq
    REQUIRE(Util::eq_float(-0.0004f, 0.f, 4)); // negative to positive, round down
    REQUIRE(!Util::eq_float(3.4567f, 3.4564f, 4));
}
