/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef FORMAT_JSON_HPP
#define FORMAT_JSON_HPP

#include <optional>
#include <sstream>
#include <string>
#include <tuple>

#include "../values/value.hpp"
#include "parse.hpp"

class Json final : public ValueFormat {
private:
    // Note: We take some liberties with JSON format:
    // 1. JSON doesn't have comments, but // and /* */ are common, so we will parse (but *never* output) them
    // 2. JSON has no representation for inf, nan, but "Infinity", "-Infinity", "NaN" coerce to expected in JavaScript.

    std::optional<char> skipWhitespace(LineHandler& handler) const;

    /// @brief Parse and return the string
    /// The initial " is expected to already have been seen and skipped.
    /// @param handler the handler info to parse from
    /// @return the string parsed
    std::string parseString(LineHandler& handler) const;

    [[nodiscard]] Value* parseValue(LineHandler& handler);

    void printKey(std::stringstream& out, const std::string& key) const;

    void printValue(std::stringstream& out, const Value& value, unsigned indents) const;

protected:
    inline SpecialFloatResult isSpecialFloat(LineHandler& handle) const override {
        // JSON parsing handles these cases by itself (within strings)
        return SpecialFloatResult::F_NONE;
    }

    void verifyBlank(LineHandler& handler) noexcept(false) override;

    void parseFile(ValueMap& vars, LineHandler& handler) override;

    [[nodiscard]] std::tuple<std::string, Value*> parseVariable(LineHandler& handler) noexcept(false) override;

public:
    void printFile(std::stringstream& out, const ValueMap& vars) override;
};
#endif
