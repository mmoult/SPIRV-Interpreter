/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cctype> // for std::isspace
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "../values/type.hpp"
#include "../values/value.hpp"
export module format.toml;
import format.parse;
import value.aggregate;
import value.primitive;

export class Toml : public ValueFormat {

private:
    std::tuple<char, bool> skipWhitespace(LineHandler& handler) const {
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid)
                return std::tuple(c, valid);
            else if (c == '#') { // comment until end of line
                while (c != '\n' && valid) {
                    auto res = handler.next();
                    c = std::get<0>(res);
                    valid = std::get<1>(res);
                }
                continue;
            } else if (!std::isspace(c))
                return std::tuple(c, valid);; // not whitespace!
            handler.skip(1);
        }
    }

    Value* parseStruct(LineHandler& handler) {
        // skip over the {, which should have been seen already
        handler.skip(1);
        // A list of key-val pairs
        std::vector<const Value*> elements;
        std::vector<std::string> names;
        while (true) {
            auto [c, valid] = skipWhitespace(handler);
            if (!valid)
                throw std::runtime_error("End found while parsing struct!");

            if (c == '}') {
                // Consume the ]
                handler.skip(1);
                break;
            }

            // Parse a name = value pair, but the (name =) part is optional.
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '"' || c == '\'') {
                std::string name = parseName(handler);
                auto [c1, valid1] = skipWhitespace(handler);
                if (!valid1 || c1 != '=') {
                    std::stringstream err;
                    err << "Missing definition for struct memeber \"" << name << "\"!";
                    throw std::runtime_error(err.str());
                }
                handler.skip(1);
                auto [_, valid2] = skipWhitespace(handler);
                if (!valid2) {
                    std::stringstream err;
                    err << "Missing value in definition for struct member \"" << name << "\"!";
                    throw std::runtime_error(err.str());
                }
                names.push_back(name);
            } else
                names.push_back("");

            // Parse out an element
            const Value* element = parse(handler);
            elements.push_back(element);

            // Allow comma after each element (even after final element)
            auto [c2, valid2] = skipWhitespace(handler);
            if (valid2) {
                if (c2 == ',')
                    handler.skip(1);
                else if (c2 != '}')
                    throw std::runtime_error("Missing comma between elements in struct");
            }
        }
        // Now that we are done parsing, add names and elements to the struct instance
        std::vector<const Type*> el_type_list;
        for (const auto val : elements) {
            const Type& vt = val->getType();
            el_type_list.push_back(&vt);
        }
        Type ts = Type::structure(el_type_list);
        for (unsigned i = 0; i < names.size(); ++i)
            ts.nameMember(i, names[i]);
        Struct* st = new Struct(ts);
        st->addElements(elements);
        return st;
    }

    Value* parseArray(LineHandler& handler) {
        // skip over the [, which should have been seen already
        handler.skip(1);
        // A list of elements to add. Unlike SPIR-V, which will tell us the type ahead of time, we
        // parse all elements then decide the type from what we see.
        std::vector<const Value*> elements;
        while (true) {
            auto [c, valid] = skipWhitespace(handler);
            if (!valid)
                throw std::runtime_error("End found while parsing array!");

            if (c == ']') {
                // Consume the ]
                handler.skip(1);
                break;
            }

            // Parse out an element
            const Value* element = parse(handler);
            elements.push_back(element);

            // Allow comma after each element (even after final element)
            auto [c2, valid2] = skipWhitespace(handler);
            if (valid2) {
                if (c2 == ',')
                    handler.skip(1);
                else if (c2 != ']')
                    throw std::runtime_error("Missing comma between elements in array");
            }
        }
        // Now that we are done parsing, add elements and form the type:
        try {
            Type union_type = Type::unionOf(elements);
            Type* ut = new Type(union_type);
            Array* arr = new Array(*ut, elements.size());
            arr->addElements(elements);
            return arr;
        } catch(const std::exception& e) {
            for (auto* element: elements)
                delete element;
            throw std::runtime_error("Element parsed of incompatible type with other array elements!");
        }
    }

    Value* parse(LineHandler& handler) {
        // 1. number (which may begin with +, -, or .) or may be inf or nan
        // 2. bool (true or false)
        // 3. array (using [] syntax)
        // 4. struct (using {member = value} syntax)
        // Strings and dates (in TOML spec) not supported
        while (true) {
            auto [c, valid] = skipWhitespace(handler);
            if (!valid)
                break;

            if (c == '[')
                return parseArray(handler);
            else if (c == '{')
                return parseStruct(handler);

            // Note: true, false, inf, nan are forbidden field names
            else if (handler.matchId("true"))
                return new Primitive(true);
            else if (handler.matchId("false"))
                return new Primitive(false);

            // If it isn't an array, struct, or bool, it must be a number!
            return parseNumber(handler);
        }
        throw std::runtime_error("Missing value!");
    }

    std::string parseName(LineHandler& handler) const {
        std::stringstream name;
        // 0 = none, 1 = ", 2 = '
        unsigned in_str = 0;
        bool escape = false;
        // NOTE: This algorithm doesn't handle the case where a mid-name dot can be surrounded by spaces
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid)
                break;

            if (in_str > 0) {
                if (in_str == 1) {
                    if (c == '\\') {
                        escape = !escape;
                        if (escape) {
                            handler.skip(1);
                            continue;
                        }
                    } else if (c == '"' && !escape) {
                        in_str = 0;
                        handler.skip(1);
                        continue;
                    }
                } else if (in_str == 2 && c == '\'') {
                    in_str = 0;
                    handler.skip(1);
                    continue;
                }
                name << c;
            } else if (c == '"')
                in_str = 1;
            else if (c == '\'')
                in_str = 2;
            else if (std::isspace(c))
                break;
            else if (c == '#') { // start of comment, effectively a newline
                skipWhitespace(handler);
                break;
            } else
                name << c;
            
            handler.skip(1);
        }
        return name.str();
    }

protected:
    void verifyBlank(LineHandler& handler) noexcept(false) override {
        // Used when the value to parse has already been finalized. Should only find whitespace or
        // comment(s) remaining before newline or invalid
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid || c == '#' || c == '\n')
                break;
            else if (!std::isspace(c)) {
                std::stringstream err;
                err << "Unexpected character (" << c << ") found after value!";
                throw std::runtime_error(err.str());
            }
            handler.skip(1);
        }
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override {
        while (true) {
            auto [c, valid] = skipWhitespace(handler);
            if (!valid)
                break;

            // Expect an identifier
            std::string name = parseName(handler);

            // After the name, look for =
            auto [c1, valid1] = skipWhitespace(handler);
            if (c1 != '=' || !valid1) {
                std::stringstream err;
                err << "Missing '=' in definition of variable \"" << name << "\"!";
                throw std::runtime_error(err.str());
            }
            handler.skip(1);

            // Lastly, determine the type of the value
            parseValue(vars, name, handler);

            // Verify that there is nothing else before EOL
            verifyBlank(handler);
        }
        // Empty file is permissible.
    }

    void parseValue(ValueMap& vars, std::string& key, LineHandler& handle) noexcept(false) override {
        Value* val = parse(handle);
        addToMap(vars, key, val);
    }

public:

    void printFile(std::stringstream& out, const ValueMap& vars) override {
        //
    }
};
