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
#include <limits> // for inf and nan
#include <iostream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

export module toml;
import type;
import value;
import value.aggregate;
import value.primitive;


enum class IdValidity {
    VALID,
    BREAK,
    INVALID,
};
IdValidity is_ident(char c, bool first) {
    if ((c >= 'A' && c <= 'Z') || c == '_' || (c >= 'a' && c <= 'z'))
        return IdValidity::VALID;
    if (!first && (c >= '0' && c <= '9'))
        return IdValidity::VALID;
    if (std::isspace(c))
        return IdValidity::BREAK;
    return IdValidity::INVALID;
}

template<typename Ite, typename Sen>
class LineHandler {
    Ite* begin;
    Sen* end;

    const std::string* pLine;
    unsigned idx;

    using MayChar = std::tuple<char, bool>;

public:
    LineHandler(const std::string* start_line, unsigned start_idx, Ite* begin, Sen* end):
        begin(begin),
        end(end),
        pLine(start_line),
        idx(start_idx) {}

    MayChar next() {
        MayChar res = peek();
        ++idx;
        return res;
    }

    /// @brief Try to match current location to the given string.
    /// If it is a match, advance the handler's pointer for the next parse. Also, check that the string to match is
    /// independent in the search line, that is, there are no other identifier characters immediately after it.
    /// @param match the string to match current location against
    /// @param len the length of string match
    /// @return whether the match was successful
    bool matchId(const char* match, unsigned len) {
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
        if (idx + len == line.length() || is_ident(line[idx + len], false) != IdValidity::VALID) {
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
        return std::tuple(pLine, idx);
    }

    void skip(unsigned delta) {
        idx += delta;
    }
    void setIdx(unsigned i) {
        idx = i;
    }

    MayChar peek() {
        while (true) {
            if (pLine == nullptr) {
                // fetch a new line
                if (begin == nullptr || *begin == *end)
                    return std::tuple(0, false);
                ++(*begin);
                pLine = &(**begin);
                idx = 0; // reset point to front
            }

            const auto& line = *pLine;
            for (; idx < line.length(); ++idx) {
                char c = line[idx];
                if (c == '#')
                    break; // beginning of line comment, skip rest of line
                if (std::isspace(c))
                    continue;

                return std::tuple(c, true);
            }

            pLine = nullptr; // reset to ask for new line
        }
    }
};

bool parse_int_max(
    std::integral auto& val,
    const std::integral auto max,
    const std::string& line,
    unsigned start,
    unsigned end
) {
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

export class Toml {

private:
    // Putting these functions in the class allows them to be reference each other recursively

    /// @brief Parse a number from the given index in the provided line
    /// @param line the string to parse from
    /// @param i the starting index in the line
    /// @param bool the sign of the number (true if prefixed by + or no prefix, false if prefixed by -).
    ///             The sign, if present, must be the first character.
    /// @return the number parsed (could be signed, unsigned, or float)
    template<typename Ite, typename Sen>
    static Value* parse_number(LineHandler<Ite, Sen>& handler) {
        auto [c, valid] = handler.peek();
        if (!valid) {
            std::cerr << "Missing number!" << std::endl;
            return nullptr;
        }

        // Very first, we may see a sign signifier
        bool sign = true;
        if (c == '+' || c == '-') {
            sign = (c == '+');
            // character accepted, move on to next
            handler.skip(1);
        }

        // Next, we check for special nums (inf and nan)
        if (handler.matchId("inf", 3)) {
            return new Primitive(std::numeric_limits<float>::infinity() * (sign? 1: -1));
        } else if (handler.matchId("nan", 3)) {
            return new Primitive(std::numeric_limits<float>::quiet_NaN() * (sign? 1: -1));
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
                if (has_dot) {
                    std::cerr << "Found number with multiple decimals!" << std::endl;
                    return nullptr;
                } else if (e_sign != 0) {
                    std::cerr << "Ill-formatted number with decimal in exponent!" << std::endl;
                    return nullptr;
                }
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
                            std::cerr << "Unexpected character (" << c << ") found in exponent of number!" << std::endl;
                            return nullptr;
                        }
                    } else {
                        std::cerr << "Missing exponent in number after " << line[e] << "!" << std::endl;
                        return nullptr;
                    }
                } else {
                    std::cerr << "Ill-formatted number!" << std::endl;
                    return nullptr;
                }
            } else if (std::isspace(c) || c == ',' || c == ']' || c == '}')
                break;
            else {
                std::cerr << "Unexpected character (" << c << ") in number!" << std::endl;
                return nullptr;
            }
        }
        if (idx == end) {
            // No characters were accepted!
            std::cerr << "No number found before break!" << std::endl;
            return nullptr;
        }
        // Here at the end, we want to parse out from the indices we have learned
        if (!has_dot && e_sign == 0) {
            // Integral type- use either int or uint
            if (sign) {
                // Assume the larger uint type
                uint32_t val = 0;
                if (!parse_int_max(val, UINT32_MAX, line, idx, end)) {
                    std::cerr << "Value parsed is too big to fit in a 32-bit uint!" << std::endl;
                    return nullptr;
                }
                handler.setIdx(end);
                return new Primitive(val);
            } else {
                // Use int to apply the negation
                int32_t val = 0;
                bool too_small = false;
                // compare with logic in parse_int_max
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
                if (too_small) {
                    std::cerr << "Value parsed is too small to fit in a 32-bit int!" << std::endl;
                    return nullptr;
                }
                handler.setIdx(end);
                return new Primitive(val);
            }
        }else {
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
                if (!parse_int_max(exp, std::numeric_limits<int>::max(), line, ii, end)) {
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

    template<typename Ite, typename Sen>
    static Value* parse_struct(LineHandler<Ite, Sen>& handler) {
        //Struct* strct = new Struct();
        // TODO parsing of struct
        return nullptr; //strct;
    }

    template<typename Ite, typename Sen>
    static Value* parse_array(LineHandler<Ite, Sen>& handler) {
        // skip over the [, which should have been seen already
        handler.next();
        // A list of elements to add. Unlike SPIR-V, which will tell us the type ahead of time, we
        // parse all elements then decide the type from what we see.
        std::vector<const Value*> elements;
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid) {
                std::cerr << "End found while parsing array!" << std::endl;
                return nullptr;
            }

            if (c == ']') {
                // Consume the ]
                handler.skip(1);
                break;
            }

            // Parse out an element
            const Value* element = parse(handler);
            if (element == nullptr) {
                // Assume that the element already printed its error
                std::cerr << "Could not parse element in array!" << std::endl;
                return nullptr;
            }
            elements.push_back(element);

            // Allow comma after each element (even after final element)
            auto [c2, valid2] = handler.peek();
            if (valid2) {
                if (c2 == ',')
                    handler.skip(1);
                else if (c2 != ']') {
                    std::cerr << "Missing comma between elements in array" << std::endl;
                    return nullptr;
                }
            }
        }
        // Now that we are done parsing, add elements and form the type:
        try {
            Type union_type = Type::unionOf(elements);
            Array* arr = new Array(union_type, elements.size());
            arr->addElements(elements);
            return arr;
        } catch(const std::exception& e) {
            std::cerr << "Element parsed of incompatible type with other array elements!" << std::endl;
            for (auto* element: elements)
                delete element;
            return nullptr;
        }
    }

    template<typename Ite, typename Sen>
    static Value* parse(LineHandler<Ite, Sen>& handler) {
        // 1. number (which may begin with +, -, or .) or may be inf or nan
        // 2. bool (true or false)
        // 3. array (using [] syntax)
        // 4. struct (using {member = value} syntax)
        // Strings and dates (in TOML spec) not supported
        while (true) {
            auto [c, valid] = handler.peek();
            if (!valid)
                break;

            if (c == '[')
                return parse_array(handler);
            else if (c == '{')
                return parse_struct(handler);

            // Note: true, false, inf, nan are forbidden field names
            else if (handler.matchId("true", 4))
                return new Primitive(true);
            else if (handler.matchId("false", 4))
                return new Primitive(false);

            // If it isn't an array, struct, or bool, it must be a number!
            return parse_number(handler);
        }
        std::cerr << "Missing value!" << std::endl;
        return nullptr; // found nothing!
    }

    static bool add_to_map(ValueMap& vars, std::string key, Value* val) {
        // If the map already has the key, we have a problem
        if (vars.contains(key)) {
            std::cerr << "Attempt to add variable \"" << key << "\" when one by the same name already exists!" << std::endl;
            return false;
        }
        vars[key] = val;
        return true;
    }

    template<typename Ite, typename Sen>
    static bool parse_for(
        ValueMap& vars,
        std::string key,
        const std::string*& line,
        unsigned& idx,
        Ite* begin,
        Sen* end
    ) {
        // Handle string input through the LineHandler
        LineHandler handle(line, idx, begin, end);
        Value* val = parse(handle);
        if (val == nullptr)
            return false;

        // Now fetch the updates from the handler
        auto [l, i] = handle.update();
        line = l;
        idx = i;

        return add_to_map(vars, key, val);
    }

public:
    template<std::input_or_output_iterator Ite, std::sentinel_for<Ite> Sen>
    static bool parse_toml(ValueMap& vars, Ite begin, Sen end) {
        while (begin != end) {
            const std::string* pline = &*begin;

            std::string name = "";
            bool equals = false;
            bool val_end = false;
            for (unsigned i = 0; i < pline->length(); ++i) {
                const std::string& line = *pline;
                char c = line[i];
                if (std::isspace(c))
                    continue;
                else if (c == '#')
                    break; // begin of line comment, skip rest of line
                else if (c == '=' && !name.empty()) {
                    if (equals) {
                        std::cerr << "Found another = when TOML value expected instead!" << std::endl;
                        return false;
                    }
                    equals = true;
                } else {
                    if (val_end) {
                        // Found something after value end!
                        std::cerr << "Found character (" << c << ") after value where end expected!" << std::endl;
                        return false;
                    }
                    // The character is not a separator, so it must be a name or a value
                    else if (name.empty()) { // need name
                        if (is_ident(c, true) == IdValidity::VALID) {
                            unsigned start = i++;
                            for (; i < line.length(); i++) {
                                switch (is_ident(line[i], false)) {
                                    case IdValidity::VALID:
                                        continue;
                                    case IdValidity::INVALID:
                                        std::cerr << "Character (" << line[i] <<
                                                    ") found where alphanumeric or break expected!" << std::endl;
                                        return false;
                                    case IdValidity::BREAK:
                                        // end of name
                                        goto after_loop;
                                }
                            }
                            after_loop:
                            name = line.substr(start, i - start);
                            // Re-handle the character which signaled a break
                            --i;
                        } else {
                            std::cerr << "Character (" << line[i] << ") found where alphanumeric expected!" << std::endl;
                            return false;
                        }
                    } else {
                        if (!equals) {
                            std::cerr << "Missing = before value for \"" << name << "\"!" << std::endl;
                            return false;
                        }
                        // need value:
                        if (!parse_for(vars, name, pline, i, &begin, &end))
                            return false;

                        val_end = true; // should not see anything else syntactically relevant from here to EOL
                    }
                }
            }

            ++begin;
        }
        // Empty file (ie no variables defined) is legal
        return true;
    }

    /// @brief Parse value from string val and add to value map with the given key name
    /// @param vars the map of pre-existing variables
    /// @param key the name of the value to parse
    /// @param val the string to parse the value from
    /// @return whether the TOML value was properly formed
    static bool parse_toml_value(ValueMap& vars, std::string key, std::string val) {
        // trim any whitespace on key
        bool trim = false;
        unsigned begin = 0;
        for (; begin < key.length(); ++begin) {
            if (!std::isspace(key[begin]))
                break;
            trim = true;
        }
        unsigned end = key.length() - 1;
        for (; end >= begin + 1; --end) {
            if (!std::isspace(key[end]))
                break;
            trim = true;
        }
        if (trim)
            key = key.substr(begin, (end + 1) - begin);

        unsigned i = 0;
        const std::string* pVal = &val; // need an r-value pointer even though the value cannot change given inputs
        using Iter = std::vector<std::string>::iterator;
        if (!parse_for(vars, key, pVal, i, static_cast<Iter*>(nullptr), static_cast<Iter*>(nullptr)))
            return false;
        // verify that we only see whitespace or comments after the value
        for (; i < val.length(); ++i) {
            if (std::isspace(i))
                continue;
            else if (val[i] == '#') // start of comment, skip rest
                break;
            else {
                std::cerr << "Unexpected character (" << val[i] << ") found after value!" << std::endl;
                return false;
            }
        }
        return true;
    }
};
