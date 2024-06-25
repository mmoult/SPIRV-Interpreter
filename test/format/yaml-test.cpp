/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cstdint>
#include <map>
#include <string>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include "../../src/values/value.hpp"
import format.parse;
import format.yaml;
import value.aggregate;
import value.primitive;


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

    SECTION("array in array") {
        SETUP
        std::vector<const Value*> news;
        Type fp32 = Type::primitive(DataType::FLOAT);

        std::vector<const Value*> inners;
        for (unsigned j = 0; j < 3; ++j) {
            std::vector<const Value*> es;
            for (unsigned i = 0; i < 5; ++i) {
                const Value* prim = new Primitive(static_cast<float>(i + j));
                news.push_back(prim);
                es.push_back(prim);
            }
            Array* inner = new Array(fp32, es.size());
            news.push_back(inner);
            inner->addElements(es);
            inners.push_back(static_cast<const Array*>(inner));
        }
        Array outer(inners[0]->getType(), inners.size());
        outer.addElements(inners);
        vars["foo"] = &outer;

        format.printFile(out, vars);
        REQUIRE(out.str() ==
            "foo: \n"
            "- \n"
            "  - 0.0\n"
            "  - 1.0\n"
            "  - 2.0\n"
            "  - 3.0\n"
            "  - 4.0\n"
            "- \n"
            "  - 1.0\n"
            "  - 2.0\n"
            "  - 3.0\n"
            "  - 4.0\n"
            "  - 5.0\n"
            "- \n"
            "  - 2.0\n"
            "  - 3.0\n"
            "  - 4.0\n"
            "  - 5.0\n"
            "  - 6.0\n"
            );

        for (const Value* val : news)
            delete val;
    }

    SECTION("atypical indent") {
        SETUP
        Yaml sformat;
        sformat.setIndentSize(5);

        constexpr unsigned arr_size = 6;
        std::vector<Primitive> prims;
        prims.reserve(arr_size);
        Type fp32 = Type::primitive(DataType::UINT);

        std::vector<const Value*> es;
        for (unsigned i = 0; i < arr_size; ++i) {
            prims.emplace_back(static_cast<uint32_t>(i));
            // It should be fine to access the vector pointer directly because we should never assign more than the
            // initial allocation.
            es.push_back(&prims[i]);
        }
        Array test(fp32, es.size());
        test.addElements(es);
        vars["array"] = &test;

        sformat.printFile(out, vars);
        REQUIRE(out.str() ==
            "array: \n"
            "-    0\n"
            "-    1\n"
            "-    2\n"
            "-    3\n"
            "-    4\n"
            "-    5\n"
            );
    }

/*
    SECTION("struct in array") {

    }

    SECTION("array in struct") {

    }
*/
#undef SETUP
}

void circle_test(Yaml& yaml, const std::string& key, Value& value, std::string result) {
    std::map<std::string, const Value*> print;
    std::stringstream out;
    print[key] = &value;
    yaml.printFile(out, print);
    REQUIRE(out.str() == (result + '\n'));

    std::map<std::string, const Value*> read;
    static_cast<ValueFormat*>(&yaml)->parseVariable(read, result);
    REQUIRE(read.size() == print.size());
    REQUIRE(read.size() == 1);
    for (const auto& [key, val] : print) {
        for (const auto& [key2, val2] : print) {
            REQUIRE(key == key2);
            if (!val->equals(*val2)) {
                std::stringstream failure;
                failure << "  ";
                yaml.printFile(failure, read);
                failure << "!=\n  ";
                yaml.printFile(failure, print);
                FAIL(failure.str());
            }
        }
    }
}

// Verify that input -> output -> input preserves data
TEST_CASE("i/o", "[yaml]") {
    Yaml format;

    SECTION("challenging keys") {
        // We are testing the key, so the value doesn't really matter
        Primitive test(-1);

        // Yaml can handle an identifier with spaces
        circle_test(format, "something or other", test, "something or other: -1");
        // Must go in quotes for now since our parser "tokenizes" by the first character.
        circle_test(format, "1start", test, "'1start': -1");
        // Single quote is complicated. If the string requires at least single quote, the presence of a single quote
        // must upgrade that to double quote. I don't want to deal with that complexity yet, so I just default to double
        circle_test(format, "Bob's_favorite", test, "\"Bob\\'s_favorite\": -1");
        // Notice that we don't need any quotes around the string. The subquote is interpreted as part of the whole
        circle_test(format, "quote \"Here\"", test, "quote \"Here\": -1");
        circle_test(format, "tricky: has colon", test, "'tricky: has colon': -1");
    }

}


