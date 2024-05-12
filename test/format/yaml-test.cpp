#include <cstdint>
#include <map>
#include <string>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include "../../src/values/value.hpp"
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
        vars["some array"] = &test;

        sformat.printFile(out, vars);
        REQUIRE(out.str() ==
            "\"some array\": \n"
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
