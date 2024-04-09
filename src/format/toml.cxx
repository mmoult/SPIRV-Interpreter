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
import value.pointer;
import value.primitive;

export class Toml : public ValueFormat {

private:
    /// @brief Skips whitespace until the next newline character
    /// Note that newlines can be syntactically relevant to end the key-value pair
    /// @param handler to parse from
    /// @return (char, valid) of next non-whitespace or newline
    std::tuple<char, bool> skipWhitespace(LineHandler& handler, bool breakAtNewline = false) const {
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid)
                return std::tuple(c, valid);
            else if (c == '#') { // comment until end of line
                while (valid && c != '\n') {
                    auto res = handler.next();
                    c = std::get<0>(res);
                    valid = std::get<1>(res);
                }
                if (breakAtNewline)
                    return std::tuple(c, valid);
            } else if (!std::isspace(c) || (breakAtNewline && c == '\n'))
                return std::tuple(c, valid); // semantically relevant character
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
            const Value* element = parseValue(handler);
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
            const Value* element = parseValue(handler);
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
        } catch (const std::exception& e) {
            for (auto* element : elements)
                delete element;
            throw std::runtime_error("Element parsed of incompatible type with other array elements!");
        }
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

    void verifyBlank(LineHandler& handler, bool breakAtNewline) {
        while (true) {
            auto [c, valid] = skipWhitespace(handler, breakAtNewline);
            handler.skip();
            if (!valid)
                break;
            else if (c == '\n') {
                // Should only be triggered if break at newline true
                break;
            } else if (!std::isspace(c)) {
                std::stringstream err;
                err << "Unexpected character (" << c << ") found after value!";
                throw std::runtime_error(err.str());
            }
        }
    }

    void printNameTag(std::stringstream& out, const std::string& name, unsigned indents = 0) const {
        // Try to print the name without any quotes, but it may be needed
        bool quote_needed = name.empty();
        for (unsigned i = 0; i < name.length(); ++i) {
            if (std::isspace(name[i])) {
                quote_needed = true;
                break;
            }
        }
        if (quote_needed)
            out << "\"";
        out << name;
        if (quote_needed)
            out << "\"";
        out << " = ";
    }

    void printValue(std::stringstream& out, const Value& value, unsigned indents = 0) const {
        const auto& type_base = value.getType().getBase();
        switch (type_base) {
        case DataType::FLOAT:
            out << static_cast<const Primitive&>(value).data.fp32;
            break;
        case DataType::UINT:
            out << static_cast<const Primitive&>(value).data.u32;
            break;
        case DataType::INT:
            out << static_cast<const Primitive&>(value).data.i32;
            break;
        case DataType::BOOL:
            if (static_cast<const Primitive&>(value).data.i32)
                out << "true";
            else
                out << "false";
            break;
        case DataType::STRUCT:
        case DataType::ARRAY: {
            char open, close;
            bool is_struct = type_base == DataType::STRUCT;
            const std::vector<std::string>* names;
            if (is_struct) {
                open = '{';
                close = '}';
                names = &value.getType().getNames();
            } else {
                open = '[';
                close = ']';
            }
            out << open;

            const auto& agg = static_cast<const Aggregate&>(value);
            if (agg.getSize() > 0) {
                // If any subelement is nested, print each on its own line
                bool nested = false;
                for (const auto& element: agg) {
                    if (isNested(*element)) {
                        nested = true;
                        break;
                    }
                }

                unsigned nindents = indents + 1;
                if (nested) {
                    for (unsigned i = 0; i < agg.getSize(); ++i) {
                        const auto& element = *agg[i];
                        newline(out, nindents);

                        if (is_struct)
                            printNameTag(out, (*names)[i], nindents);
                        printValue(out, element, nindents);

                        out << ',';
                    }
                    newline(out, indents);
                } else {
                    out << " ";
                    bool first = true;
                    for (unsigned i = 0; i < agg.getSize(); ++i) {
                        const auto& element = *agg[i];
                        if (first)
                            first = false;
                        else
                            out << ", ";

                        if (is_struct)
                            printNameTag(out, (*names)[i], nindents);
                        printValue(out, element, nindents);
                    }
                    out << " ";
                }
            }
            out << close;
            break;
        }
        case DataType::POINTER: {
            const auto& pointer = static_cast<const Pointer&>(value);
            out << "[" << pointer.getHead();
            for (unsigned idx : pointer.getIndices())
                out << ", " << idx;
            out << "]";
            break;
        }
        default: // VOID, FUNCTION
            throw std::runtime_error("Cannot print value!");
        }
    }

    bool isNested(const Value& val) const {
        const auto base = val.getType().getBase();
        return base == DataType::STRUCT || base == DataType::ARRAY || base == DataType::POINTER;
    }

protected:
    Value* parseValue(LineHandler& handler) noexcept(false) override {
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

    void verifyBlank(LineHandler& handler) noexcept(false) override {
        verifyBlank(handler, false);
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override {
        while (true) {
            auto [c, valid] = skipWhitespace(handler);
            if (!valid)
                break;
            if (c == '\n') {
                handler.skip();
                continue;
            }

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
            Value* val = parseValue(handler);
            addToMap(vars, name, val);

            // Verify that there is nothing else before EOL
            verifyBlank(handler, true);
        }
        // Empty file is permissible.
    }

public:
    void printFile(std::stringstream& out, const ValueMap& vars) override {
        bool first = true;
        for (const auto& [name, value] : vars) {
            if (first)
                first = false;
            else
                out << '\n';

            printNameTag(out, name);
            printValue(out, *value);
            out << '\n';
        }
    }
};
