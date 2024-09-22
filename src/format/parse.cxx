/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cctype> // for std::isspace
#include <cmath>
#include <concepts> // for std::integral
#include <cstdint> // for uint32_t and int32_t
#include <istream>
#include <limits> // for inf and nan
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../values/value.hpp"
export module format.parse;
import value.aggregate;
import value.primitive;

export class ValueFormat {

private:
    /// @brief parse an integer from the given string
    /// Assumes that all characters in range [start, end) have been pre-checked as 0-9 (otherwise we wouldn't)
    /// know with certainty that this is an integer).
    /// @param val the integer parsed
    /// @param max the maximum for the integer's type
    /// @param line the line to parse from
    /// @param start the index in line of the first relevant character
    /// @param end the index in line after the last relevant character
    /// @return whether the parse was successful. Fails if the integer parsed does not fit within the max size.
    bool parseIntWithMax(
        std::integral auto& val,
        const std::integral auto max,
        const std::string& line,
        unsigned start,
        unsigned end
    ) const {
        for (unsigned i = start; i < end; ++i) {
            // Since we pre-checked the input, we know the char is 0-9
            char digit = line[i] - '0';
            if (max / 10 < val) {
                return false;
            }
            val *= 10;
            if (max - digit < val) {
                return false;
            }
            val += digit;
        }
        return true;
    }

protected:
    // Output settings
    bool templatize = false;
    bool preferTabs = true;
    unsigned indentSize = 2;

    class LineHandler {
        std::istream* file;
        std::string fromFile;
        const std::string* pLine;
        unsigned idx;

        enum class IdValidity {
            VALID,
            BREAK,
            INVALID,
        };
        IdValidity isIdent(char c, bool first) const {
            if ((c >= 'A' && c <= 'Z') || c == '_' || c == '-' || (c >= 'a' && c <= 'z'))
                return IdValidity::VALID;
            if (!first && ((c >= '0' && c <= '9')))
                return IdValidity::VALID;
            if (std::isspace(c))
                return IdValidity::BREAK;
            return IdValidity::INVALID;
        }

    public:
        LineHandler(const std::string* start_line, unsigned start_idx, std::istream* file):
            file(file),
            pLine(start_line),
            idx(start_idx)
        {
            if (pLine == nullptr && file != nullptr) {
                // Load the first line
                idx = 1;
                peek();
            }
        }

        /// @brief Returns the next character (with its validity) and moves the string pointer to the next
        /// @return (character, is_valid)
        std::optional<char> next() {
            auto res = peek();
            ++idx;
            return res;
        }

        /// @brief Try to match current location to the given string.
        /// If it is a match, advance the handler's pointer for the next parse. Also, check that the string to match is
        /// independent in the search line, that is, there are no other identifier characters immediately after it.
        /// @param match the string to match current location against
        /// @param len the length of string match
        /// @return whether the match was successful
        bool matchId(std::string match) {
            auto c = peek();
            if (!c.has_value())
                return false;

            unsigned len = match.length();
            const auto& line = *pLine;
            if (idx + len > line.length()) // if the match string is longer than remaining length on line
                return false;

            // speculatively look ahead
            for (unsigned i = 0; i < len; ++i) {
                if (line[i + idx] != match[i])
                    return false;
            }
            // We need to verify that the character after len (if any) is not alphanumeric
            // If it is, then the search string was only a prefix, not a valid reference

            // Either the match goes to the end of line (in which case, there are no chars after) OR
            // there is another character to check at (idx + len)
            if (idx + len == line.length() || isIdent(line[idx + len], false) != IdValidity::VALID) {
                idx += len; // update the index to point to after the constant name
                return true;
            }
            return false;
        }

        /// @brief Return info for modified variables
        /// Specifically, return the current line and index. The iterator (if any) is modified in place and the iterator end
        /// should not have been modified.
        /// @return the info for variables changed during line handling
        std::tuple<const std::string*, unsigned> update() {
            return {pLine, idx};
        }

        void resetToLineStart() {
            // If this is a multi-line approach, we can just go to the beginning of this line
            if (file != nullptr)
                idx = 0;
            else {
                // Otherwise, we need to backtrack to the last newline
                for (; idx > 0; --idx) {
                    if ((*pLine)[idx] == '\n')
                        break;
                }
            }
        }

        void skip(unsigned delta = 1) {
            idx += delta;
        }
        void setIdx(unsigned i) {
            idx = i;
        }

        /// @brief fetches (but does not advance beyond) the next character.
        std::optional<char> peek() {
            if (pLine == nullptr) {
                // fetch a new line
                if (file == nullptr || !std::getline(*file, fromFile))
                    return {};

                pLine = &fromFile;
                idx = 0; // reset point to front
            }

            const auto& line = *pLine;
            if (!line.empty() && idx < line.length())
                return {line[idx]};

            pLine = nullptr; // reset to ask for new line
            return {'\n'};
        }
    };

    bool isNested(const Value& val) const {
        const auto base = val.getType().getBase();
        return base == DataType::STRUCT || base == DataType::ARRAY || base == DataType::POINTER;
    }

    /// @brief Prints a newline character then the number of spaces needed for desired indentation
    /// @param out the stream to write to
    /// @param spaces whether to force the use of spaces
    /// @param indents the number of indentations, where each indentation is two spaces
    void newline(std::stringstream& out, bool spaces, unsigned indents) const {
        out << '\n';
        if (spaces || !preferTabs)
            out << std::string(indentSize * indents, ' ');
        else
            out << std::string(indents, '\t');
    }

    /// @brief Adds the key-value pair to the map, throwing an error if the key has already been mapped
    /// @param vars variables to add this key-value pair to
    /// @param key the name of the value
    /// @param val the value associated with the given key
    void addToMap(ValueMap& vars, std::string key, Value* val) const {
        // If the map already has the key, we have a problem
        if (vars.contains(key)) {
            std::stringstream err;
            err << "Attempt to add variable \"" << key << "\" when one by the same name already exists!";
            throw std::runtime_error(err.str());
        }
        vars[key] = val;
    }

    Value* constructArrayFrom(std::vector<const Value*>& elements) {
        try {
            Type union_type = elements.empty() ? Type() : Type::unionOf(elements);
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

    Value* constructStructFrom(std::vector<std::string>& names, std::vector<const Value*>& elements) {
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

    /// @brief Parse a number from the given index in the provided line
    /// @param handler the line handler used to parse from
    /// @return the number parsed (could be signed, unsigned, or float)
    Value* parseNumber(LineHandler& handler) noexcept(false) {
        auto c = handler.peek();
        if (!c.has_value())
            throw std::runtime_error("Missing number!");

        // Very first, we may see a sign signifier
        bool sign = true;
        if (*c == '+' || *c == '-') {
            sign = (*c == '+');
            // character accepted, move on to next
            handler.skip(1);
        }

        // Next, we check for special nums (inf and nan)
        switch (isSpecialFloat(handler)) {
        case SpecialFloatResult::F_INF:
            return new Primitive(std::numeric_limits<float>::infinity() * (sign? 1: -1));
        case SpecialFloatResult::F_NAN:
            return new Primitive(std::numeric_limits<float>::quiet_NaN() * (sign? 1: -1));
        default:
            break;
        }

        // From here, we need more details than line handler gives us, so we take control
        auto [pline, idx] = handler.update();
        const auto& line = *pline;

        bool has_dot = false;
        int e_sign = 0;
        unsigned e;
        unsigned end = idx;
        for (; end < line.length(); ++end) {
            char c = line[end];
            if (c >= '0' && c <= '9')
                continue;
            else if (c == '.') {
                if (has_dot)
                    throw std::runtime_error("Found number with multiple decimals!");
                else if (e_sign != 0)
                    throw std::runtime_error("Ill-formatted number with decimal in exponent!");

                has_dot = true;
            } else if (c == 'e' || c == 'E') {
                if (e_sign == 0) {
                    e = end;
                    // Look ahead to find the exponent's sign
                    if (++end < line.length()) {
                        c = line[end];
                        if (c == '-')
                            e_sign = -1;
                        else if (c == '+' || (c >= '0' && c <= '9'))
                            e_sign = 1;
                        else {
                            std::stringstream err;
                            err << "Unexpected character (" << c << ") found in exponent of number!";
                            throw std::runtime_error(err.str());
                        }
                    } else {
                        std::stringstream err;
                        err << "Missing exponent in number after " << line[e] << "!";
                        throw std::runtime_error(err.str());
                    }
                } else
                    throw std::runtime_error("Ill-formatted number!");
            } else if (std::isspace(c) || c == ',' || c == ']' || c == '}' || c == '"' || c == '\'')
                break;
            else {
                std::stringstream err;
                err << "Unexpected character (" << c << ") in number!";
                throw std::runtime_error(err.str());
            }
        }
        if (idx == end)
            // No characters were accepted!
            throw std::runtime_error("No number found before break!");

        // Here at the end, we want to parse out from the indices we have learned
        if (!has_dot && e_sign == 0) {
            // Integral type- use either int or uint
            if (sign) {
                // Assume the larger uint type
                uint32_t val = 0;
                if (!parseIntWithMax(val, UINT32_MAX, line, idx, end))
                    throw std::runtime_error("Value parsed is too big to fit in a 32-bit uint!");

                handler.setIdx(end);
                return new Primitive(val);
            } else {
                // Use int to apply the negation
                int32_t val = 0;
                bool too_small = false;
                // compare with logic in parseIntWithMax
                for (unsigned ii = idx; ii < end; ++ii) {
                    char digit = line[ii] - '0';
                    if (INT32_MIN / 10 > val) {
                        too_small = true;
                        break;
                    }
                    val *= 10;
                    if (INT32_MIN + digit > val) {
                        too_small = true;
                        break;
                    }
                    val -= digit;
                }
                if (too_small)
                    throw std::runtime_error("Value parsed is too small to fit in a 32-bit int!");

                handler.setIdx(end);
                return new Primitive(val);
            }
        } else {
            // float parsing, which may include exponent after the first digit
            float val = 0;
            unsigned early_end = end;
            float move_dec = 0;
            if (e_sign != 0)
                early_end = e;
            for (unsigned ii = idx; ii < early_end; ++ii) {
                char c = line[ii];
                if (c >= '0' && c <= '9') {
                    val *= 10;
                    val += c - '0';
                } else if (c == '.') {
                    unsigned to_move = early_end - (ii + 1);
                    move_dec = -static_cast<float>(to_move);
                    continue; // We will move the decimal later
                }
            }
            handler.setIdx(end); // float parsing cannot fail from here out
            if (!sign)
                val *= -1;
            // Process exponent, if any
            if (e_sign != 0) {
                int exp = 0;
                unsigned ii = e + 1; // skip the e
                // if the exponent was negative, there must be a '-' to skip
                // if positive, check for an optional sign
                if (e_sign < 0 || line[ii] == '+')
                    ++ii;
                if (!parseIntWithMax(exp, std::numeric_limits<int>::max(), line, ii, end)) {
                    // If the exponent was out of bounds, we can do some approximation
                    // Out of bounds positive exponent -> inf
                    // Out of bounds negative exponent -> 0
                    // multiply by val to keep the original sign
                    val *= (e_sign >= 0) ? std::numeric_limits<float>::infinity(): 0.0;
                    return new Primitive(val);
                }
                // Otherwise, the exponent was in bounds, so we can move the decimal appropriately
                // Relies on signed int having a |min| >= |max|
                // (Or in other words, loses a miniscule amount of precision for -exp)
                // In fact, I am confident float cannot hold an exponent large enough to make any
                // difference here.

                move_dec += exp * (e_sign >= 0? 1 : -1);
            }
            // Finally, adjust the decimal (there is a decimal because otherwise we would parse integral)
            val *= std::pow(10, move_dec);
            return new Primitive(val);
        }
    }

    enum class SpecialFloatResult {
        F_NONE, F_INF, F_NAN,
    };
    /// @brief Give the derived class an opportunity to handle special floats while parsing a number
    /// @param handle the handle to read from
    /// @return one of none, infinity, or NaN
    virtual SpecialFloatResult isSpecialFloat(LineHandler& handle) = 0;

    /// @brief Parse and return a single key-value pair
    /// @param vars variables to save to- a map of names to values
    /// @param handle a handler to parse the value from
    virtual std::tuple<std::string, Value*> parseVariable(LineHandler& handle) = 0;

    /// @brief Parse all key-value pairs given in the file. Save each into the map of values
    /// @param vars variables to save to- a map of names to values
    /// @param handler a handler to parse the value from
    virtual void parseFile(ValueMap& vars, LineHandler& handler) = 0;

    /// @brief Throw error if any characters before the parse end (signalled by an invalid character) are non-space.
    /// Called after the completion of parseVariable for single-string inputs. May be reused by other methods.
    /// @param handler the line handler used to parse from
    virtual void verifyBlank(LineHandler& handle) = 0;

public:
    void setTemplate(bool print_template) {
        templatize = print_template;
    }
    void setIndentSize(unsigned size_in_spaces) {
        indentSize = size_in_spaces;
        preferTabs = false;
    }

    /// @brief Parse values from the given file
    /// @param vars the map of pre-existing variables. Also the map new values are saved to
    void parseFile(ValueMap& vars, std::istream& file) noexcept(false) {
        LineHandler handle(nullptr, 0, &file);
        parseFile(vars, handle);
    }

    /// @brief Parse value from string val and add to value map with the given key name
    /// @param vars the map of pre-existing variables. Also the map new values are saved to
    /// @param key the name of the value to parse
    /// @param val the string to parse the value from
    void parseVariable(ValueMap& vars, std::string keyval) noexcept(false) {
        const std::string* pstr = &keyval; // need an r-value pointer even though the value should not change
        LineHandler handle(pstr, 0, nullptr);
        auto [key, value] = parseVariable(handle);
        addToMap(vars, key, value);
        verifyBlank(handle); // Verify there is only whitespace or comments after
    }

    virtual void printFile(std::stringstream& out, const ValueMap& vars) = 0;
};
