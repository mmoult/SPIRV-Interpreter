/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef FORMAT_YAML_HPP
#define FORMAT_YAML_HPP

#include <optional>
#include <sstream>
#include <string>
#include <tuple>

#include "../values/value.hpp"
#include "parse.hpp"

class Yaml final : public ValueFormat {
private:
    /// @brief Skips whitespace
    /// @param handler to parse from
    /// @param break_at_newline whether to stop at newlines (true) or treat them as regular space (false)
    /// @return (char, valid) of next non-whitespace or newline
    std::optional<char> skipWhitespace(LineHandler& handler, bool break_at_newline) const;

    [[nodiscard]] std::tuple<std::string, Value*>
    parseVariable(LineHandler& handler, unsigned min_indent, bool end_check = true);

    [[nodiscard]] std::tuple<Value*, bool>
    parseAgg(LineHandler& handler, unsigned indent, bool list, std::string seen_name = "");

    [[nodiscard]] Value* parseInlineAgg(LineHandler& handler, bool list);

    std::string parseString(LineHandler& handler) const;

    void verifyBlank(LineHandler& handler, bool break_at_newline);

    void printKey(std::stringstream& out, const std::string& name) const;

    inline void
    printKeyValue(std::stringstream& out, const std::string& key, const Value& value, unsigned indents) const {
        printKey(out, key);
        out << ':';
        printValue(out, value, indents);
    }

    inline void printArrayIndent(std::stringstream& out) const {
        out << '-';
        // Subtract two from the ident size since:
        // - each element must prefix its value with a space (to avoid trailing)
        // - the "-" character itself takes a space
        if (indentSize > 2)
            out << std::string(indentSize - 2, ' ');
    }

    void printValue(std::stringstream& out, const Value& value, unsigned indents = 0, bool can_compact = false) const;

    unsigned countIndent(LineHandler& handler, bool break_at_newline = false) const;

    [[nodiscard]] std::tuple<Value*, bool> parseValue(LineHandler& handler, unsigned min_indent);

protected:
    inline SpecialFloatResult isSpecialFloat(LineHandler& handler) const override {
        if (handler.matchId(".inf") || handler.matchId(".Inf"))
            return SpecialFloatResult::F_INF;
        if (handler.matchId(".NAN"))
            return SpecialFloatResult::F_NAN;
        return SpecialFloatResult::F_NONE;
    }

    [[nodiscard]] inline std::tuple<std::string, Value*> parseVariable(LineHandler& handler) override {
        unsigned indent = countIndent(handler);
        return parseVariable(handler, indent);
    }

    inline void verifyBlank(LineHandler& handler) noexcept(false) override {
        verifyBlank(handler, false);
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override;

public:
    void printFile(std::stringstream& out, const ValueMap& vars) override;
};
#endif
