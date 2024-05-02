#include <map>
#include <string>
#include <sstream>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "../../src/values/value.hpp"
import format.json;
import value.aggregate;
import value.primitive;

TEST_CASE("output", "[json]") {
#define SETUP \
    std::map<std::string, const Value*> vars; \
    std::stringstream out;

    Json format;

    SECTION("empty") {
        SETUP
        format.printFile(out, vars);
        REQUIRE(out.str() == "{\n}\n");
    }

    SECTION("1 num") {
        SETUP
        Primitive num(2);
        vars["foo"] = &num;
        format.printFile(out, vars);
        REQUIRE(out.str() ==
            "{\n"
            "  \"foo\" : 2\n"
            "}\n"
        );
    }

    SECTION("4 prims") {
        SETUP
        Primitive first(true);
        Primitive second(-0.2f);
        Primitive third(-3);
        Primitive fourth(false);
        vars["first"] = &first;
        vars["second"] = &second;
        vars["third"] = &third;
        vars["fourth"] = &fourth;
        format.printFile(out, vars);
        REQUIRE(out.str() ==
            "{\n"
            "  \"first\" : true,\n"
            "  \"fourth\" : false,\n"
            "  \"second\" : -0.2,\n"
            "  \"third\" : -3\n"
            "}\n"
        );
    }

    SECTION("inline array") {
        SETUP
        Type fp32 = Type::primitive(DataType::FLOAT);
        Array arr(fp32, 4);
        std::vector<const Value*> es;
        const Primitive prims[] = {
            Primitive(3.14f),
            Primitive(1.59f),
            Primitive(2.65f),
            Primitive(3.59f)
        };
        for (unsigned i = 0; i < 4; ++i)
            es.push_back(&prims[i]);
        arr.addElements(es);
        Primitive bar(7);

        vars["bar"] = &bar;
        vars["arr"] = &arr;
        format.printFile(out, vars);
        REQUIRE(out.str() ==
            "{\n"
            "  \"arr\" : [ 3.14, 1.59, 2.65, 3.59 ],\n"
            "  \"bar\" : 7\n"
            "}\n"
        );
    }

    SECTION("inline struct") {
        SETUP
        std::vector<const Type*> struct_fields;
        const Type first = Type::primitive(DataType::INT);
        const Type second = Type::primitive(DataType::BOOL);
        const Type third = Type::primitive(DataType::FLOAT);
        struct_fields.push_back(&first);
        struct_fields.push_back(&second);
        struct_fields.push_back(&third);
        Type struct_type = Type::structure(struct_fields);
        std::string names[] = {"first", "second", "third"};
        for (unsigned i = 0; i < 3; ++i)
            struct_type.nameMember(i, names[i]);
        Struct foo(struct_type);

        std::vector<const Value*> fields;
        const Primitive firstp(-8);
        const Primitive secondp(true);
        const Primitive thirdp(0.09f);
        fields.push_back(&firstp);
        fields.push_back(&secondp);
        fields.push_back(&thirdp);
        foo.addElements(fields);
        vars["spaced and \\ name"] = &foo;
        format.printFile(out, vars);
        REQUIRE(out.str() ==
            "{\n"
            // About the four backslashes: since they occur in a string literal, there are only 2 in the result.
            // The JSON output requires two because they are within a string literal- one escapes the other.
            "  \"spaced and \\\\ name\" : { \"first\" : -8, \"second\" : true, \"third\" : 0.09 }\n"
            "}\n"
        );
    }

#undef SETUP
}
