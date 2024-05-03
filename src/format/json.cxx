/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

#include "../values/value.hpp"
export module format.json;
import format.parse;
import value.aggregate;
import value.pointer;
import value.primitive;

export class Json : public ValueFormat {
private:
    // Note: We take some liberties with JSON format:
    // 1. JSON doesn't have comments, but // and /* */ are common, so we will parse (but *never* output) them
    // 2. JSON has no representation for inf, nan, but "Infinity", "-Infinity", "NaN" coerce to expected in JavaScript.

    std::tuple<char, bool> skipWhitespace(LineHandler& handler) const {
        // newlines are not ever relevant in JSON, so we skip them with all whitespace
        char last = '\0';
        bool in_comment = false;

        while (true) {
            auto [c, valid] = handler.peek();

            if (!valid)
                break;
            else if (in_comment) {
                if (last == '*' && c == '/') {
                    in_comment = false;
                    // even though the last is a slash, it cannot be used to form comments (is already consumed).
                    last = '\0';
                } else
                    last = c;
            } else if (!std::isspace(c)) {
                if (last == '/' && c == '*')
                    in_comment = true;
                else if (last == '/' && c == '/') {
                    // Line comment: proceed to next newline
                    while (c != '\n' && valid) {
                        auto res = handler.next();
                        c = std::get<0>(res);
                        valid = std::get<1>(res);
                    }
                    if (!valid)
                        break;
                } else if (c != '/') // If c is a slash, it could be the start of a comment
                    return std::tuple(c, valid);

                last = c;
            }
            handler.skip(1);
        }

        // If we end and last was / out of comment, it is NOT blank
        if (!in_comment && last == '/')
            throw std::runtime_error("Character '/' found in string expected to be blank!");
        // It is possible to have an unterminated comment (in_comment == true), but we won't worry about it here

        return std::tuple(0, false);
    }

    /// @brief Parse and return the name found in the next string
    /// The initial " is expected to already have been seen and skipped.
    /// @param handler the handler info to parse from
    /// @return the string parsed
    std::string parseName(LineHandler& handler) const {
        std::stringstream name;
        bool escape = false;
        while (true) {
            auto [c3, v3] = handler.next();
            if (!v3)
                throw std::runtime_error("Unterminated name string in JSON!");

            if (escape) {
                escape = false;
                // TODO support \u0123 hex chars
                // The 3 cases (", \, /") can print normally, intentional fallthrough for them
                if (c3 != '"' && c3 != '\\' && c3 != '/') {
                    switch (c3) {
                    case 'b':
                        name << '\b';
                        break;
                    case 'f':
                        name << '\f';
                        break;
                    case 'n':
                        name << '\n';
                        break;
                    case 'r':
                        name << '\r';
                        break;
                    case 't':
                        name << '\t';
                        break;
                    default:
                        std::stringstream err;
                        err << "Unknown escape sequence in JSON string: \\" << c3 << "!";
                        throw std::runtime_error(err.str());
                    }
                    continue; // do not fall through to print
                }
            } else if (c3 == '"')
                break;
            else if (c3 == '\\') {
                escape = true;
                continue;
            }
            name << c3;
        }
        return name.str();
    }

    Value* parseValue(LineHandler& handler) {
        auto [c0, v0] = skipWhitespace(handler);
        if (!v0)
            throw std::runtime_error("Missing value!");
        if (c0 == '{') {
            handler.skip();
            std::vector<std::string> names;
            std::vector<const Value*> values;
            bool first = true;
            while (true) {
                auto [c1, v1] = skipWhitespace(handler);
                if (!v1)
                    throw std::runtime_error("Missing '}' in JSON input!");
                if (c1 == '}')
                    break;
                if (!first) {
                    if (c1 != ',')
                        throw std::runtime_error("Missing comma to delimit entries in JSON object!");
                    handler.skip();
                } else
                    first = false;
                auto [key, value] = parseVariable(handler);
                names.push_back(key);
                values.push_back(static_cast<const Value*>(value));
            }
            handler.skip();  // skip over the closing brace
            return constructStructFrom(names, values);
        } else if (c0 == '[') {
            handler.skip();
            std::vector<const Value*> values;
            bool first = true;
            while (true) {
                auto [c1, v1] = skipWhitespace(handler);
                if (!v1)
                    throw std::runtime_error("Missing ']' in JSON input!");
                if (c1 == ']')
                    break;
                if (!first) {
                    if (c1 != ',')
                        throw std::runtime_error("Missing comma to delimit entries in JSON array!");
                    handler.skip();
                } else
                    first = false;
                auto value = parseValue(handler);
                values.push_back(static_cast<const Value*>(value));
            }
            handler.skip();
            return constructArrayFrom(values);
        } else if (c0 == '\"') {
            // No strings supported besides inf or nan
            handler.skip();
            std::string name = parseName(handler);
            if (name == "NaN")
                return new Primitive(std::numeric_limits<float>::quiet_NaN());
            float inf = std::numeric_limits<float>::infinity();
            if (name == "Infinity")
                return new Primitive(inf);
            else if (name == "-Infinity")
                return new Primitive(-inf);
            std::stringstream err;
            err << "String in JSON input not supported: \"" << name << "\"!";
            throw std::runtime_error(err.str());
        } else if (handler.matchId("true"))
            return new Primitive(true);
        else if (handler.matchId("false"))
            return new Primitive(false);
        else
            return parseNumber(handler);
    }

    void printKey(std::stringstream& out, const std::string& key) const {
        // JSON doesn't have any kind of literal-forced string, so we must manually escape all characters as they
        // come up.
        out << '"';
        std::array special{'\b', '\f', '\n', '\r', '\t'};
        std::array match{"\\b", "\\f", "\\n", "\\r", "\\t"};
        for (unsigned i = 0; i < key.length(); ++i) {
        next_char:
            char c = key[i];
            for (unsigned j = 0; j < special.size(); ++j) {
                char spec = special[j];
                if (c == spec) {
                    out << match[j];
                    goto next_char;
                }
            }
            if (c == '"' || c == '\\')
                out << '\\';
            out << c;
        }
        out << '"';
    }

    void printValue(std::stringstream& out, const Value& value, unsigned indents) const {
        const auto& type_base = value.getType().getBase();
        switch (type_base) {
        case DataType::FLOAT: {
            if (templatize) {
                out << "<float>";
                break;
            }
            float fp = static_cast<const Primitive&>(value).data.fp32;
            if (std::isinf(fp)) {
                if (fp >= 0)
                    out << "\"Infinity\"";
                else
                    out << "-\"Infinity\"";
            } else if (std::isnan(fp)) {
                out << "\"NaN\"";
            } else
                out << fp;
            break;
        }
        case DataType::UINT:
            if (templatize) {
                out << "<uint>";
                break;
            }
            out << static_cast<const Primitive&>(value).data.u32;
            break;
        case DataType::INT:
            if (templatize) {
                out << "<int>";
                break;
            }
            out << static_cast<const Primitive&>(value).data.i32;
            break;
        case DataType::BOOL:
            if (templatize) {
                out << "<bool>";
                break;
            }
            if (static_cast<const Primitive&>(value).data.b32)
                out << "true";
            else
                out << "false";
            break;
        case DataType::STRUCT:
        case DataType::ARRAY: {
            char close;
            bool is_struct = type_base == DataType::STRUCT;
            unsigned inline_max;
            const std::vector<std::string>* names;
            if (is_struct) {
                out << '{';
                close = '}';
                names = &value.getType().getNames();
                inline_max = 2;
            } else {
                out << '[';
                close = ']';
                inline_max = 4;
            }

            const auto& agg = static_cast<const Aggregate&>(value);
            // If any subelement is nested, print each on its own line
            unsigned agg_size = agg.getSize();
            bool each_line = agg_size > inline_max || agg_size == 0;
            if (!each_line) {
                for (const auto& element: agg) {
                    if (isNested(*element)) {
                        each_line = true;
                        break;
                    }
                }
            }
            for (unsigned i = 0; i < agg_size; ++i) {
                const auto& element = *agg[i];
                if (i > 0)
                    out << ',';
                if (each_line)
                    newline(out, false, indents + 1);
                else
                    out << ' ';

                if (is_struct) {
                    printKey(out, (*names)[i]);
                    out << " : ";
                }

                printValue(out, element, indents + 1);
            }
            if (each_line)
                newline(out, false, indents);
            else
                out << " "; // space the final value from the end brace
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

protected:
    SpecialFloatResult isSpecialFloat(LineHandler& handle) override {
        // JSON parsing handles these cases by itself (within strings)
        return SpecialFloatResult::F_NONE;
    }

    void verifyBlank(LineHandler& handler) noexcept(false) override {
        auto [c, valid] = skipWhitespace(handler);
        if (!valid)
            return;

        std::stringstream err;
        err << "Unexpected character (" << c << ") found after value!";
        throw std::runtime_error(err.str());
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override {
        auto [c1, v1] = skipWhitespace(handler);
        if (!v1 || c1 != '{')
            throw std::runtime_error("JSON file must begin with '{'!");
        handler.skip();

        // {
        //   "name": value,
        // }
        // I don't think JSON allows trailing commas, and file can be empty
        bool first = true;
        while (true) {
            auto [c3, v3] = skipWhitespace(handler);
            if (!v3)
                throw std::runtime_error("Missing '}' in JSON file!");
            if (c3 == '}')
                break;

            if (first)
                first = false;
            else {
                // We must see a comma to separate multiple values!
                if (c3 != ',')
                    throw std::runtime_error("Missing , to delimit entries in JSON file!");
                handler.skip();
            }

            // If we didn't see an end, we must see a named value
            auto [key, val] = parseVariable(handler);
            addToMap(vars, key, val);
        }
        handler.skip();
        verifyBlank(handler);
    }

    std::tuple<std::string, Value*> parseVariable(LineHandler& handler) noexcept(false) override {
        auto [c2, v2] = skipWhitespace(handler);
        if (c2 != '"' || !v2)
            throw std::runtime_error("Named value in JSON input must begin with '\"'!");
        handler.skip();
        std::string name = parseName(handler);
        auto [c3, v3] = skipWhitespace(handler);
        if (!v3 || c3 != ':')
            throw std::runtime_error("Missing colon after JSON name!");
        handler.skip();
        Value* val = parseValue(handler);
        return std::tuple(name, val);
    }

public:
    void printFile(std::stringstream& out, const ValueMap& vars) override {
        out << "{";
        bool first = true;
        for (const auto& [key, val] : vars) {
            if (first)
                first = false;
            else
                out << ",";
            newline(out, false, 1);
            printKey(out, key);
            out << " : ";
            printValue(out, *val, 1);
        }
        newline(out, false, 0);
        out << "}\n";
    }
};
