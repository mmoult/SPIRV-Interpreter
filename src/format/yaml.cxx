/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cctype> // for std::isspace
#include <cmath> // for std::isinf and std::isnan
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "../values/type.hpp"
#include "../values/value.hpp"
export module format.yaml;
import format.parse;
import value.aggregate;
import value.pointer;
import value.primitive;

export class Yaml : public ValueFormat {

private:
    /// @brief Skips whitespace
    /// @param handler to parse from
    /// @param breakAtNewline whether to stop at newlines (true) or treat them as regular space (false)
    /// @return (char, valid) of next non-whitespace or newline
    std::tuple<char, bool> skipWhitespace(LineHandler& handler, bool breakAtNewline) const {
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

    std::tuple<std::string, Value*> parseVariable(LineHandler& handler, unsigned min_indent, bool end_check = true) {
        std::string key = parseName(handler);
        auto [c1, v1] = skipWhitespace(handler, true);
        if (!v1 || c1 != ':') {
            std::stringstream err;
            err << "Missing colon in definition for '" << key << "'!";
            throw std::runtime_error(err.str());
        }
        handler.skip();
        auto [val, next_line] = parseValue(handler, min_indent);
        // queue up the next line (and verify there is no more content on this)
        if (!next_line && end_check)
            verifyBlank(handler, true);
        return std::tuple(key, val);
    }

    Value* constructListFrom(std::vector<const Value*>& elements) {
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

    Value* constructMapFrom(std::vector<std::string>& names, std::vector<const Value*>& elements) {
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

    std::tuple<Value*, bool> parseAgg(LineHandler& handler, unsigned indent, bool list) {
        std::vector<const Value*> elements;
        std::vector<std::string> names;
        // We have already seen the indent at the start of the first element
        // The indent has been saved as indent
        while (true) {
            const Value* celement;
            if (list) {
                // Must see '-' and then some optional space
                auto [c0, _] = handler.peek();
                if (c0 != '-')
                    // The list is done because this line doesn't have a bullet. This cannot happen on the first element
                    // because we must see a bullet to get to this logic.
                    break;
                handler.skip();
                // We should know validity because we checked when first identifying or asserting indentation
                skipWhitespace(handler, true);
                auto [element, new_line] = parseValue(handler, indent);
                if (!new_line)
                    verifyBlank(handler, true);
                celement = static_cast<const Value*>(element);
            } else {
                auto [key, val] = parseVariable(handler, indent);
                names.push_back(key);
                celement = static_cast<const Value*>(val);
            }
            elements.push_back(celement);

            // parseVariable or verifyBlank have taken the courtesy of going to the next line for us.
            // We want to see if the next line has the correct indent or if it is out of this aggregate
            unsigned next = countIndent(handler);
            // next == 0 if we reached end of file
            if (next < indent)
                break;
            else if (next > indent) {
                // We cannot suddenly get a block with a larger indent
                std::stringstream err;
                err << "Encountered block while parsing aggregate with indent " << next << " where ";
                err << indent << " was expected!";
                throw std::runtime_error(err.str());
            }
        }
        // Reset to the start of the line so the next to process has the correct indent count
        handler.resetToLineStart();
        // Now that we are done parsing, add elements and form the type:
        return std::tuple(list? constructListFrom(elements): constructMapFrom(names, elements), true);
    }

    Value* parseInlineAgg(LineHandler& handler, bool list) {
        // skip over the [ or {, which has already been seen
        handler.skip();
        std::vector<const Value*> elements;
        std::vector<std::string> names;
        while (true) {
            auto [c, valid] = skipWhitespace(handler, false);
            if (!valid)
                throw std::runtime_error("Premature end found while parsing aggregate!");

            if ((list && c == ']') || (!list && c == '}')) {
                // Consume the end token
                handler.skip();
                break;
            }

            // Parse out an element
            const Value* celement;
            if (list) {
                auto [element, new_line] = parseValue(handler, 0);
                // We don't expect inline lists to rollover, but if they do, we don't care about it so long as we
                // see a concluding ]
                celement = static_cast<const Value*>(element);
            } else {
                auto [key, val] = parseVariable(handler, 0, false);
                names.push_back(key);
                celement = static_cast<const Value*>(val);
            }
            elements.push_back(celement);

            // Allow comma after each element ((even after final element))
            auto [c2, valid2] = skipWhitespace(handler, false);
            if (valid2) {
                if (c2 == ',')
                    handler.skip(1);
                else if ((list && c2 != ']') || (!list && c2 != '}'))
                    throw std::runtime_error("Missing comma between elements in inline aggregate!");
            }
        }
        // Now that we are done parsing, add elements and form the type:
        if (list)
            return constructListFrom(elements);
        return constructMapFrom(names, elements);
    }

    std::string parseName(LineHandler& handler) const {
        std::stringstream name;
        // 0 = none, 1 = ", 2 = '
        unsigned in_str = 0;
        bool escape = false;
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
            else if (std::isspace(c) || c == '#' || c == ':') // start of comment is effectively newline
                break;
            else
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

    void printKey(std::stringstream& out, const std::string& name) const {
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
    }

    void printKeyValue(std::stringstream& out, const std::string& key, const Value& value, unsigned indents) const {
        printKey(out, key);
        out << ": ";
        // If element is an array, we have a special exception
        // (This is because the element bullet "- " serves as the indent)
        unsigned nindents = indents + ((value.getType().getBase() == DataType::ARRAY)? 0 : 1);
        printValue(out, value, nindents);
    }

    void printValue(std::stringstream& out, const Value& value, unsigned indents = 0) const {
        const auto& type_base = value.getType().getBase();
        switch (type_base) {
        case DataType::FLOAT: {
            float fp = static_cast<const Primitive&>(value).data.fp32;
            if (std::isinf(fp)) {
                if (fp >= 0)
                    out << ".inf";
                else
                    out << "-.inf";
            } else if (std::isnan(fp)) {
                out << ".NAN";
            } else
                out << fp;
            break;
        }
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

            const auto& agg = static_cast<const Aggregate&>(value);
            if (unsigned agg_size = agg.getSize(); agg_size > 0) {
                // If any subelement is nested, print each on its own line
                bool nested = false;
                for (const auto& element: agg) {
                    if (isNested(*element)) {
                        nested = true;
                        break;
                    }
                }

                if (nested || agg_size > 4) {
                    for (unsigned i = 0; i < agg.getSize(); ++i) {
                        const auto& element = *agg[i];
                        newline(out, indents);

                        if (is_struct)
                            printKeyValue(out, (*names)[i], element, indents);
                        else {
                            out << "- ";
                            printValue(out, element, indents + 1);
                        }
                    }
                    break;
                }
                // If we did not print, fall through intentionally
            }

            // Inline print
            out << open << " ";
            bool first = true;
            for (unsigned i = 0; i < agg.getSize(); ++i) {
                const auto& element = *agg[i];
                if (first)
                    first = false;
                else
                    out << ", ";

                if (is_struct) {
                    printKey(out, (*names)[i]);
                    out << ": ";
                }
                printValue(out, element, indents + 1);
            }
            out << " " << close;
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

    unsigned countIndent(LineHandler& handler) const {
        unsigned indent = 0;
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid)
                return 0;
            else if (c == '#') { // comment until end of line. count indent on next
                while (valid && c != '\n') {
                    auto res = handler.next();
                    c = std::get<0>(res);
                    valid = std::get<1>(res);
                }
                indent = 0; // reset indent because we are on next line
            } else if (c == ' ') { // YAML only allows space for indents
                ++indent;
            } else if (c == '\n') {
                // Blank line, so its indent is irrelevant
                indent = 0; // restart for next linestd::tuple<std::string, Value*>
            } else // encountered semantically relevant character
                return indent;
            handler.skip(1);
        }
    }

    std::tuple<Value*, bool> parseValue(LineHandler& handler, unsigned min_indent) {
        while (true) {
            auto [c, valid] = skipWhitespace(handler, true);
            if (!valid)
                break;

            // Inline lists or maps
            if (c == '[')
                return std::tuple(parseInlineAgg(handler, true), false);
            else if (c == '{')
                return std::tuple(parseInlineAgg(handler, false), false);
            else if (c == '\n') {
                // Nothing on this line, so it must be an aggregate
                unsigned next = countIndent(handler);
                if (next < min_indent) {
                    std::stringstream err;
                    err << next << " indents seen in block expecting at least " << min_indent << "!";
                    throw std::runtime_error(err.str());
                }
                // If we see a -, then this is a list. Otherwise, it is a map
                auto [c1, v1] = handler.peek();
                if (!v1)
                    break; // missing value
                return parseAgg(handler, next, c1 == '-');
            }

            // Note: true, false are forbidden field names
            else if (handler.matchId("true"))
                return std::tuple(new Primitive(true), false);
            else if (handler.matchId("false"))
                return std::tuple(new Primitive(false), false);

            // If it isn't an array, struct, or bool, it must be a number!
            return std::tuple(parseNumber(handler), false);
        }
        throw std::runtime_error("Missing value!");
    }

protected:
    SpecialFloatResult isSpecialFloat(LineHandler& handler) override {
        if (handler.matchId(".inf") || handler.matchId(".Inf"))
            return SpecialFloatResult::F_INF;
        if (handler.matchId(".NAN"))
            return SpecialFloatResult::F_NAN;
        return SpecialFloatResult::F_NONE;
    }

    std::tuple<std::string, Value*> parseVariable(LineHandler& handler) override {
        unsigned indent = countIndent(handler);
        auto [key, val] = parseVariable(handler, indent);
        return std::tuple(key, val);
    }

    void verifyBlank(LineHandler& handler) noexcept(false) override {
        verifyBlank(handler, false);
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override {
        while (true) {
            unsigned i = countIndent(handler);
            if (i > 0) {
                std::stringstream err;
                err << "Variable at file root defined at indent " << i << "!";
                throw std::runtime_error(err.str());
            }
            auto [c, valid] = handler.peek();
            if (!valid)
                break;

            auto [key, val] = parseVariable(handler, 0);
            addToMap(vars, key, val);
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

            printKeyValue(out, name, *value, 0);
            out << '\n';
        }
    }
};
