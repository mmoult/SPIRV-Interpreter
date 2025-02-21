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
    SECTION("empty") {
        Yaml format;
        std::map<std::string, const Value*> vars;
        std::stringstream out;
        format.printFile(out, vars);
        REQUIRE(out.str() == "");
    }
}

void input_test(Yaml& yaml, std::map<std::string, const Value*>& compare, std::string result) {
    std::map<std::string, const Value*> read;
    static_cast<ValueFormat*>(&yaml)->parseVariable(read, result);
    REQUIRE(read.size() == compare.size());
    for (const auto& [key, val] : compare) {
        REQUIRE(read.contains(key));
        const Value* val2 = read[key];
        if (!val->equals(*val2)) {
            std::stringstream failure;
            failure << "  ";
            yaml.printFile(failure, read);
            failure << "!=\n  ";
            yaml.printFile(failure, compare);
            FAIL(failure.str());
        }
    }
}

TEST_CASE("input", "[yaml]") {
    SECTION("long mapping in sequence") {
        std::vector<std::string> names{"foo", "bar", "baz"};
        Type t_uint = Type::primitive(DataType::UINT);
        constexpr unsigned STRUCT_SIZE = 3;
        std::vector<const Type*> sub_list;
        for (unsigned i = 0; i < STRUCT_SIZE; ++i)
            sub_list.push_back(&t_uint);
        Type mapping = Type::structure(sub_list, names);
        std::vector<Primitive> prims;
        std::vector<const Value*> first;
        std::vector<const Value*> second;
        for (uint32_t i = 0; i < STRUCT_SIZE * 2; ++i)
            prims.emplace_back(i);
        for (unsigned i = 0; i < STRUCT_SIZE * 2; ++i) {
            auto& vec = (i < STRUCT_SIZE)? first : second;
            vec.push_back(&prims[i]);
        }

        Struct idx0(mapping);
        idx0.addElements(first);
        Struct idx1(mapping);
        idx1.addElements(second);

        std::vector<const Value*> sequence_elements;
        sequence_elements.push_back(&idx0);
        sequence_elements.push_back(&idx1);
        Array sequence(mapping, 2);
        sequence.addElements(sequence_elements);

        std::map<std::string, const Value*> vars;
        vars["def"] = &sequence;
        Yaml format;
        input_test(format, vars,
            "def:\n"
            "-\n"
            "  foo: 0\n"
            "  bar: 1\n"
            "  baz: 2\n"
            "-\n"
            "  foo: 3\n"
            "  bar: 4\n"
            "  baz: 5");
    }
}

void circle_test(Yaml& yaml, const std::string& key, Value& value, std::string result) {
    std::map<std::string, const Value*> print;
    std::stringstream out;
    print[key] = &value;
    yaml.printFile(out, print);
    REQUIRE(out.str() == (result + '\n'));
    std::map<std::string, const Value*> read;
    read[key] = &value;
    input_test(yaml, read, result);
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

    SECTION("atypical indent") {
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

        circle_test(sformat, "sequence", test,
            "sequence: [\n"
            "     0, 1, 2, 3,\n"
            "     4, 5\n"
            "]");
    }

    SECTION("sequence in sequence") {
        std::map<std::string, const Value*> vars;
        std::vector<const Value*> news;
        Type fp32 = Type::primitive(DataType::FLOAT);

        std::vector<const Value*> inners;
        for (unsigned j = 0; j < 3; ++j) {
            std::vector<const Value*> es;
            for (unsigned i = 0; i < 4; ++i) {
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

        circle_test(format, "foo", outer,
            "foo:\n"
            "- [ 0.0, 1.0, 2.0, 3.0 ]\n"
            "- [ 1.0, 2.0, 3.0, 4.0 ]\n"
            "- [ 2.0, 3.0, 4.0, 5.0 ]");

        for (const Value* val : news)
            delete val;
    }

    SECTION("short mapping in sequence") {
        std::vector<std::string> names{"foo", "bar"};
        Type foo = Type::primitive(DataType::UINT);
        Type bar = Type::primitive(DataType::UINT);
        std::vector<const Type*> sub_list{&foo, &bar};
        Type mapping = Type::structure(sub_list, names);
        std::vector<Primitive> prims;
        std::vector<const Value*> first;
        std::vector<const Value*> second;
        constexpr unsigned STRUCT_SIZE = 2;
        for (uint32_t i = 0; i < STRUCT_SIZE * 2; ++i)
            prims.emplace_back(i);
        for (unsigned i = 0; i < STRUCT_SIZE * 2; ++i) {
            auto& vec = (i < STRUCT_SIZE)? first : second;
            vec.push_back(&prims[i]);
        }

        Struct idx0(mapping);
        idx0.addElements(first);
        Struct idx1(mapping);
        idx1.addElements(second);

        std::vector<const Value*> sequence_elements{&idx0, &idx1};
        Array sequence(mapping, 2);
        sequence.addElements(sequence_elements);

        circle_test(format, "abc", sequence,
            "abc:\n"
            "- { foo: 0, bar: 1 }\n"
            "- { foo: 2, bar: 3 }");
    }

    SECTION("long mapping in sequence") {
        std::vector<std::string> names{"foo", "bar", "baz"};
        Type t_uint = Type::primitive(DataType::UINT);
        constexpr unsigned STRUCT_SIZE = 3;
        std::vector<const Type*> sub_list;
        for (unsigned i = 0; i < STRUCT_SIZE; ++i)
            sub_list.push_back(&t_uint);
        Type mapping = Type::structure(sub_list, names);
        std::vector<Primitive> prims;
        std::vector<const Value*> first;
        std::vector<const Value*> second;
        for (uint32_t i = 0; i < STRUCT_SIZE * 2; ++i)
            prims.emplace_back(i);
        for (unsigned i = 0; i < STRUCT_SIZE * 2; ++i) {
            auto& vec = (i < STRUCT_SIZE)? first : second;
            vec.push_back(&prims[i]);
        }

        Struct idx0(mapping);
        idx0.addElements(first);
        Struct idx1(mapping);
        idx1.addElements(second);

        std::vector<const Value*> sequence_elements;
        sequence_elements.push_back(&idx0);
        sequence_elements.push_back(&idx1);
        Array sequence(mapping, 2);
        sequence.addElements(sequence_elements);

        circle_test(format, "def", sequence,
            "def:\n"
            "- foo: 0\n"
            "  bar: 1\n"
            "  baz: 2\n"
            "- foo: 3\n"
            "  bar: 4\n"
            "  baz: 5");
    }

    SECTION("mapping in mapping") {
        // Use three because it is long enough that we won't see inline aggregates
        constexpr unsigned STRUCT_SIZE = 3;
        // Notably, we must *not* see the compact form: that is only legal for mapping within sequence
        std::vector<std::string> first_names{"foo", "bar", "baz"};
        Type t_uint = Type::primitive(DataType::UINT);
        std::vector<const Type*> bottom_types;
        for (unsigned i = 0; i < STRUCT_SIZE; ++i)
            bottom_types.push_back(&t_uint);
        Type first_type = Type::structure(bottom_types, first_names);

        std::vector<std::string> second_names{"oof", "rab", "zab"};
        Type second_type = Type::structure(bottom_types, second_names);

        std::vector<std::string> top_names{"first", "second"};
        std::vector<const Type*> top_types{&first_type, &second_type};
        Type top_type = Type::structure(top_types, top_names);

        std::vector<const Value*> first_elements;
        std::vector<const Value*> second_elements;
        std::vector<Primitive> prims;
        for (uint32_t i = 0; i < STRUCT_SIZE * 2; ++i)
            prims.emplace_back(i);
        // Must push back all of prims before using their addresses in case we needed to expand the vector capacity
        for (unsigned i = 0; i < STRUCT_SIZE * 2; ++i) {
            auto& vec = (i < STRUCT_SIZE)? first_elements : second_elements;
            vec.push_back(&prims[i]);
        }

        Value* first = first_type.construct(first_elements);
        Value* second = second_type.construct(second_elements);
        Struct top(top_type);
        std::vector<const Value*> top_elements{first, second};
        top.addElements(top_elements);
        circle_test(format, "test", top,
            "test:\n"
            "  first:\n"
            "    foo: 0\n"
            "    bar: 1\n"
            "    baz: 2\n"
            "  second:\n"
            "    oof: 3\n"
            "    rab: 4\n"
            "    zab: 5");
    }

}
