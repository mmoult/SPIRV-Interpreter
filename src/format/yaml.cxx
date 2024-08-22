/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cctype> // for std::isspace
#include <cmath> // for std::isinf and std::isnan
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "../values/type.hpp"
#include "../values/value.hpp"
export module format.yaml;
import format.parse;
import rayTrace.accelStruct;
import value.aggregate;
import value.pointer;
import value.primitive;
import value.string;

export class Yaml : public ValueFormat {

private:
    /// @brief Skips whitespace
    /// @param handler to parse from
    /// @param breakAtNewline whether to stop at newlines (true) or treat them as regular space (false)
    /// @return (char, valid) of next non-whitespace or newline
    std::optional<char> skipWhitespace(LineHandler& handler, bool breakAtNewline) const {
        while (true) {
            auto cc = handler.peek();
            if (!cc.has_value())
                return {};

            char c = *cc;
            if (c == '#') { // comment until end of line
                do {
                    auto cc1 = handler.next();
                    if (!cc1.has_value())
                        return {};
                    c = *cc1;
                } while (c != '\n');
                if (breakAtNewline)
                    return {c};
            } else if (!std::isspace(c) || (breakAtNewline && c == '\n'))
                return {c}; // semantically relevant character
            handler.skip(1);
        }
    }

    std::tuple<std::string, Value*> parseVariable(LineHandler& handler, unsigned min_indent, bool end_check = true) {
        std::string key = parseString(handler);
        auto cc = skipWhitespace(handler, true);
        if (cc.value_or(0) != ':') {
            std::stringstream err;
            err << "Missing colon in definition for '" << key << "'!";
            throw std::runtime_error(err.str());
        }
        handler.skip();
        auto [val, next_line] = parseValue(handler, min_indent);
        // queue up the next line (and verify there is no more content on this)
        if (!next_line && end_check)
            verifyBlank(handler, true);
        return {key, val};
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
                auto c0 = handler.peek();
                if (*c0 != '-')
                    // The list is done because this line doesn't have a bullet. This cannot happen on the first element
                    // because we must see a bullet to get to this logic.
                    break;
                handler.skip();
                // We should know validity because we checked when first identifying or asserting indentation
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
        return {list? constructArrayFrom(elements): constructStructFrom(names, elements), true};
    }

    Value* parseInlineAgg(LineHandler& handler, bool list) {
        // skip over the [ or {, which has already been seen
        handler.skip();
        std::vector<const Value*> elements;
        std::vector<std::string> names;
        while (true) {
            auto cc0 = skipWhitespace(handler, false);
            if (!cc0.has_value())
                throw std::runtime_error("Premature end found while parsing aggregate!");
            char c = *cc0;

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
            auto cc1 = skipWhitespace(handler, false);
            if (cc1.has_value()) {
                char c1 = *cc1;
                if (c1 == ',')
                    handler.skip();
                else if ((list && c1 != ']') || (!list && c1 != '}'))
                    throw std::runtime_error("Missing comma between elements in inline aggregate!");
            }
        }
        // Now that we are done parsing, add elements and form the type:
        if (list)
            return constructArrayFrom(elements);
        return constructStructFrom(names, elements);
    }

    std::string parseString(LineHandler& handler) const {
        // Strings may use '' for literal strings or "" for escape sequences. If a string utilizes quotes, the quotes
        // must cover the entire string, i.e. the first and last character in the string must be the quotes.
        std::stringstream value;
        // 0 = none, 1 = ", 2 = '
        unsigned in_str = 0;
        bool first = true;
        bool escape = false;
        while (true) {
            auto cc = handler.peek();
            if (!cc.has_value())
                break;
            char c = *cc;

            if (in_str > 0) {
                if (in_str == 1) {
                    if (c == '\\') {
                        escape = !escape;
                        if (escape)
                            goto next;
                    } else if (c == '"' && !escape) {
                        handler.skip();
                        break;
                    }
                    escape = false;
                } else if (in_str == 2 && c == '\'') {
                    handler.skip();
                    break;
                }
                value << c;
            } else if (c == '\n' || c == '#' || c == ':')  // start of comment is effectively newline
                break;
            else {
                if (first) {
                    first = false;
                    if (c == '"' || c == '\'') {
                        in_str = (c == '"')? 1 : 2;
                        goto next;
                    }
                }
                value << c;
            }

        next:
            handler.skip();
        }
        return value.str();
    }

    void verifyBlank(LineHandler& handler, bool breakAtNewline) {
        while (true) {
            auto cc = skipWhitespace(handler, breakAtNewline);
            if (!cc.has_value())
                break;
            char c = *cc;
            if (c == '\n') {
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
        enum QuoteNeed {
            NONE = 0,
            SINGLE = 1,  // literal, no escaping
            DOUBLE = 2,  // escaping with backslash
        };
        QuoteNeed need = name.empty() ? SINGLE : NONE;
        for (unsigned i = 0; i < name.length(); ++i) {
            char c = name[i];
            if (i == 0 && std::isdigit(c) && need < SINGLE)
                need = SINGLE;
            else if ((
                c == ':' || c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == '&' || c == '*' ||
                c == '#' || c == '?' || c == '|' || c == '-' || c == '<' || c == '>' || c == '=' || c == '!' ||
                c == '%' || c == '@' || c == '\\'
            ) && need < SINGLE)
                need = SINGLE;
            else if ((c == '\n' || c == '\t' || c == '\'') && need < DOUBLE)
                need = DOUBLE;
        }

        switch (need) {
        case NONE:
            out << name;
            break;
        case SINGLE:
            out << "'" << name << "'";
            break;
        case DOUBLE:
            out << '"';
            for (unsigned i = 0; i < name.length(); ++i) {
                char c = name[i];
                if (c == '\t')
                    out << "\\t";
                else if (c == '\n')
                    out << "\\n";
                else if (c == '\'')
                    out << "\\'";
                else if (c == '\"')
                    out << "\\\"";
                else if (c == '\\')
                    out << "\\\\";
                else
                    out << c;
            }
            out << '"';
            break;
        }
    }

    void printKeyValue(std::stringstream& out, const std::string& key, const Value& value, unsigned indents) const {
        printKey(out, key);
        out << ": ";
        printValue(out, value, indents);
    }

    void printArrayIndent(std::stringstream& out) const {
        out << '-';
        if (indentSize > 1)
            out << std::string(indentSize - 1, ' ');
    }

    void printValue(std::stringstream& out, const Value& value, unsigned indents = 0) const {
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
                    out << ".inf";
                else
                    out << "-.inf";
            } else if (std::isnan(fp)) {
                out << ".NAN";
            } else {
                out << fp;
                // Print extra decimal in case the number would have printed without
                if (std::modf(fp, &fp) == 0)
                    out << ".0";
            }
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
            char open, close;
            bool is_struct = type_base == DataType::STRUCT;
            const std::vector<std::string>* names;
            unsigned inline_max;
            unsigned e_indents;
            if (is_struct) {
                open = '{';
                close = '}';
                names = &value.getType().getNames();
                inline_max = 2;
                e_indents = indents + 1;
            } else {
                open = '[';
                close = ']';
                inline_max = 4;
                e_indents = indents;
            }

            const auto& agg = static_cast<const Aggregate&>(value);
            unsigned agg_size = agg.getSize();
            // If any subelement is nested, print each on its own line
            bool nested = (is_struct && agg_size > inline_max) || (agg_size == 0 && !is_struct && templatize);
            if (!nested) {
                for (const auto& element: agg) {
                    if (isNested(*element)) {
                        nested = true;
                        break;
                    }
                }
            }

            if (nested) {
                if (agg_size == 0) {
                    // This is a runtime array: we want to provide a dummy element for the template
                    const Type& e_type = agg.getType().getElement();
                    Value* dummy = e_type.construct();
                    newline(out, true, e_indents);
                    printArrayIndent(out);
                    printValue(out, *dummy, indents + 1);
                    delete dummy;
                    newline(out, true, e_indents);
                    printArrayIndent(out);
                    out << "<...>";
                }

                for (unsigned i = 0; i < agg.getSize(); ++i) {
                    const auto& element = *agg[i];
                    newline(out, true, e_indents);

                    if (is_struct)
                        printKeyValue(out, (*names)[i], element, indents + 1);
                    else {
                        printArrayIndent(out);
                        printValue(out, element, indents + 1);
                    }
                }
            } else {
                // Inline print
                bool compress = agg_size > inline_max;
                if (!compress)
                    out << open << " ";
                else {
                    out << open;
                    ++e_indents;
                    newline(out, true, e_indents);
                }
                bool first = true;
                for (unsigned i = 0; i < agg_size; ++i) {
                    const auto& element = *agg[i];
                    if (first)
                        first = false;
                    else if (i % inline_max == 0) {
                        out << ",";
                        newline(out, true, e_indents);
                    } else
                        out << ", ";

                    if (is_struct)
                        printKeyValue(out, (*names)[i], element, indents);
                    else
                        printValue(out, element, indents);
                }
                if (!compress)
                    out << " " << close;
                else {
                    --e_indents;
                    newline(out, true, e_indents);
                    out << close;
                }
            }
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
        case DataType::STRING: {
            const auto& strv = static_cast<const String&>(value);
            // TODO process special chars
            out << strv.get();
            break;
        }
        case DataType::ACCEL_STRUCT: {
            const Type accel_struct_type = AccelStructManager::getExpectedType();
            Struct structure = Struct(Type::structure(accel_struct_type.getFields(), accel_struct_type.getNames()));
            structure.dummyFill();
            printValue(out, structure, 1);
            break;
        }
        default: // VOID, FUNCTION, RAY_QUERY
            throw std::runtime_error("Cannot print value!");
        }
    }

    unsigned countIndent(LineHandler& handler) const {
        unsigned indent = 0;
        while (true) {
            auto cc = handler.peek();
            if (!cc.has_value())
                return 0;
            char c = *cc;
            if (c == '#') { // comment until end of line. count indent on next
                do {
                    handler.skip(1);
                    auto cc1 = handler.peek();
                    if (!cc1.has_value())
                        return 0;
                    c = *cc1;
                } while (c != '\n');
                indent = 0; // reset indent because we are on next line
                continue;
            } else if (c == ' ') { // YAML only allows space for indents
                ++indent;
            } else if (c == '\n') {
                // Blank line, so its indent is irrelevant
                indent = 0; // restart for next linestd::tuple<std::string, Value*>
            } else // encountered semantically relevant character
                return indent;
            handler.skip();
        }
    }

    std::tuple<Value*, bool> parseValue(LineHandler& handler, unsigned min_indent) {
        while (true) {
            auto cc = skipWhitespace(handler, true);
            if (!cc.has_value())
                break;
            char c = *cc;

            // Inline lists or maps
            if (c == '[')
                return {parseInlineAgg(handler, true), false};
            else if (c == '{')
                return {parseInlineAgg(handler, false), false};
            else if (c == '\n') {
                // Nothing on this line, so it must be an aggregate
                unsigned next = countIndent(handler);
                if (next < min_indent) {
                    std::stringstream err;
                    err << next << " indents seen in block expecting at least " << min_indent << "!";
                    throw std::runtime_error(err.str());
                }
                // If we see a -, then this is a list. Otherwise, it is a map
                auto cc1 = handler.peek();
                if (!cc1.has_value())
                    break; // missing value
                return parseAgg(handler, next, *cc1 == '-');
            }

            // Note: true, false are forbidden field names
            else if (handler.matchId("true"))
                return {new Primitive(true), false};
            else if (handler.matchId("false"))
                return {new Primitive(false), false};

            // If it isn't an array, struct, or bool, it must be a number!
            return {parseNumber(handler), false};
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
        return parseVariable(handler, indent);
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
            auto cc = handler.peek();
            if (!cc.has_value())
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
