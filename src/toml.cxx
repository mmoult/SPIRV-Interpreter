module;
#include <cctype> // for std::isspace
#include <cmath>
#include <concepts> // for std::integral
#include <cstdint> // for uint32_t and int32_t
#include <limits> // for inf and nan
#include <map>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

import value;
export module toml;


bool parse_int_max(std::integral auto& val, const std::integral auto max, std::string& line, unsigned start, unsigned end) {
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
    enum class IdValidity {
        VALID,
        BREAK,
        INVALID,
    };
    static IdValidity is_ident(char c, bool first) {
        if ((c >= 'A' && c <= 'Z') || c == '_' || (c >= 'a' && c <= 'z'))
            return IdValidity::VALID;
        if (!first && (c >= '0' && c <= '9'))
            return IdValidity::VALID;
        if (std::isspace(c))
            return IdValidity::BREAK;
        return IdValidity::INVALID;
    }

    // Putting these functions in the class allows them to be reference each other recursively

    /// @brief Parse a number from the given index in the provided line
    /// @param line the string to parse from
    /// @param i the starting index in the line
    /// @param bool the sign of the number (true if prefixed by + or no prefix, false if prefixed by -).
    ///             The sign, if present, must be the first character.
    /// @return the number parsed (could be signed, unsigned, or float)
    static Value* parse_number(std::string& line, unsigned& i, bool sign) {
        // First, check for inf and nan
        if (line.length() > i + 3) {
            if (line[i] == 'i' && line[i + 1] == 'n' && line[i + 2] == 'f') {
                i += 3;
                return new Primitive(std::numeric_limits<float>::infinity() * (sign? 1: -1));
            } else if (line[i] == 'n' && line[i + 1] == 'a' && line[i + 2] == 'n') {
                i += 3;
                return new Primitive(std::numeric_limits<float>::quiet_NaN() * (sign? 1: -1));
            }
        }

        bool has_dot = false;
        int e_sign = 0;
        unsigned e;
        unsigned end = i;
        for (; end < line.length(); ++end) {
            char c = line[end];
            if (c >= '0' || c <= '9')
                continue;
            else if (c == '.') {
                if (has_dot) {
                    std::cerr << "Found number with multiple decimals! \"" << line.substr(i) << "\"" << std::endl;
                    return nullptr;
                } else if (e_sign != 0) {
                    std::cerr << "Ill-formatted number with decimal in exponent! \"" << line.substr(i) << "\"" << std::endl;
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
                    std::cerr << "Ill-formatted number! \"" << line.substr(i) << "\"" << std::endl;
                    return nullptr;
                }
            } else if (std::isspace(c))
                break;
            else {
                std::cerr << "Unexpected character (" << c << ") in number!" << std::endl;
            }
        }
        if (i == end) {
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
                if (!parse_int_max(val, UINT32_MAX, line, i, end)) {
                    std::cerr << "Value parsed is too big to fit in a 32-bit uint!" << std::endl;
                    return nullptr;
                }
                i = end;
                return new Primitive(val);
            } else {
                // Use int to apply the negation
                int32_t val = 0;
                bool too_small = false;
                // compare with logic in parse_int_max
                for (unsigned ii = i; ii < end; ++ii) {
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
                i = end;
                return new Primitive(val);
            }
        }else {
            // float parsing, which may include exponent after the first digit
            float val = 0;
            unsigned early_end = end;
            float move_dec = 0;
            if (e_sign != 0)
                early_end = e;
            for (unsigned ii = i; ii < early_end; ++ii) {
                char c = line[ii];
                if (c >= '0' && c <= '9') {
                    val *= 10;
                } else if (c == '.') {
                    unsigned to_move = early_end - (ii + 1);
                    move_dec = -static_cast<float>(to_move);
                    continue; // We will move the decimal later
                }
            }
            i = end; // float parsing cannot fail from here out
            if (!sign)
                val *= -1;
            // Process exponent, if any
            if (e_sign != 0) {
                int exp = 0;
                unsigned ii = e + 1; // skip the e
                if (e_sign > 0 && line[ii] == '+') // may be a prefixed + to skip
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
    static Value* parse_struct(std::string& line, unsigned& idx, Ite* begin, Sen* end) {

        return nullptr;
    }

    template<typename Ite, typename Sen>
    static Value* parse_array(std::string& line, unsigned& idx, Ite* begin, Sen* end) {

        return nullptr;
    }

    /// Return whether the string at start is the same as match
    static bool try_constant(std::string& line, unsigned& i, const char* match, unsigned len) {
        if (i + len > line.length())
            return false;
        for (unsigned ii = i; ii < len; ++ii) {
            if (line[ii] != line[ii])
                return false;
        }
        // We need to verify that the character after len (if any) is not alphanumeric
        // If it is, then the "constant" was only a prefix, not a valid reference
        if (i + len == line.length() || is_ident(line[len], false) == IdValidity::VALID) {
            i += len; // update the index to point to after the constant name
            return true;
        }
        return false;
    }

    template<typename Ite, typename Sen>
    static Value* parse(std::string& rem, unsigned& idx, Ite* begin, Sen* end) {
        // 1. number (which may begin with +, -, or .) or may be inf or nan
        // 2. bool (true or false)
        // 3. array (using [] syntax)
        // 4. struct (using {member = value} syntax)
        // Strings and dates (in TOML spec) not supported
        std::string& line = rem;
        bool first = true;
        while (true) {
            // Continue until we read a value
            if (!first) {
                // need to fetch a new line
                if (begin == nullptr || *begin == *end) {
                    std::cerr << "Missing value!" << std::endl;
                    return nullptr;
                }
                ++(*begin);
                line = **begin;
            }

            for (unsigned i = 0; i < line.length(); ++i) {
                char c = line[i];
                if (c == '#')
                    break; // beginning of line comment, skip rest of line
                if (std::isspace(c))
                    continue;
                
                if (c == '+' || c == '-')
                    return parse_number(line, ++i, c == '+');
                else if (c == '.' || (c >= '0' && c <= '9'))
                    return parse_number(line, i, true);
                else if (c == '[')
                    return parse_array(line, ++i, begin, end);
                else if (c == '{')
                    return parse_struct(line, ++i, begin, end);
                // It would be convenient to throw all parsing of inf and nan into parse_number
                // now, but we don't know with certainty that what starts with i or n isn't
                // actually the beginning of an identifier until we test it
                // Note: true, false, inf, nan are forbidden field names
                else if (try_constant(line, i, "true", 4))
                    return new Primitive(true);
                else if (try_constant(line, i, "false", 4))
                    return new Primitive(false);
                else if (try_constant(line, i, "inf", 3))
                    return new Primitive(std::numeric_limits<float>::infinity());
                else if (try_constant(line, i, "nan", 3))
                    return new Primitive(std::numeric_limits<float>::quiet_NaN());
                
                std::cerr << "Unexpected char (" << c << ") found while parsing value!" << std::endl;
                return nullptr;
            }

            // reached the end of this line, request another
            first = false;
        }
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
    static bool parse_for(ValueMap& vars, std::string key, std::string& line, unsigned& idx, Ite* begin, Sen* end) {
        Value* val = parse(line, idx, begin, end);
        if (val == nullptr)
            return false;
        return !add_to_map(vars, key, val);
    }

public:
    template<std::forward_iterator Ite, std::sentinel_for<Ite> Sen>
    static bool parse_toml(ValueMap& vars, Ite begin, Sen end) {
        while (begin != end) {
            const std::string& line = *begin;

            std::string name = "";
            bool equals = false;
            bool end = false;
            for (unsigned i = 0; i < line.length(); ++i) {
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
                    if (end) {
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
                        if (!parse_for(vars, name, line, i, &begin, &end))
                            return false;

                        end = true; // should not see anything else syntactically relevant from here to EOL
                    }
                }
            }
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
        using SI = std::vector<std::string>::iterator;
        if (!parse_for(vars, key, val, i, static_cast<SI*>(nullptr), static_cast<SI*>(nullptr)))
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

