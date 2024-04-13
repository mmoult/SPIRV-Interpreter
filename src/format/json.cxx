/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <sstream>
#include <string>

#include "../values/value.hpp"
export module format.json;
import format.parse;

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
            if (first)
                first = false;
            else {
                auto [c3, v3] = skipWhitespace(handler);
                // We must see a comma to separate multiple values!
                if (!v3 || c3 != ',')
                    throw std::runtime_error("Missing , to delimit entries in JSON file!");
                handler.skip();
            }

            auto [c2, v2] = skipWhitespace(handler);
            if (!v2)
                throw std::runtime_error("Missing '}' in JSON file!");
            if (c2 == '}')
                break;

            // If we didn't see an end, we must see a named value
            if (c2 != '"')
                throw std::runtime_error("Named value in JSON file must begin with '\"'!");
            handler.skip();
            /*
            std::string name = parseName(handler);

            auto [c3, v3] = skipWhitespace(handler);
            if (!v3 || c3 != ':')
                throw std::runtime_error("Missing colon after JSON name!");
            handler.skip();
            Value* val = parseValue(handler);
            addToMap(vars, name, val);
            */
        }
        handler.skip();
        verifyBlank(handler);
    }

    std::tuple<std::string, Value*> parseVariable(LineHandler& handle) noexcept(false) override {
        // TODO HERE
        return std::tuple("", nullptr);
    }

public:
    void printFile(std::stringstream& out, const ValueMap& vars) override {
        out << "{";
        newline(out, 1);


        out << "}";
    }
};
