#include <map>
#include <string>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include "../../src/values/value.hpp"
import format.yaml;

TEST_CASE("output", "[yaml]") {
#define SETUP \
    std::map<std::string, const Value*> vars; \
    std::stringstream out;

    Yaml format;

    SECTION("empty") {
        SETUP
        format.printFile(out, vars);
        REQUIRE(out.str() == "");
    }

    /*SECTION("") {

    }*/

#undef SETUP
}
