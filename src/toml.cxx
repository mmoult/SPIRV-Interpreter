module;
#include <cctype> // for std::isspace
#include <limits>
#include <map>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

import value;
export module toml;


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

    static Value* parse_number(std::string& line, unsigned& i, int sign) {

        return nullptr;
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
                    return parse_number(line, ++i, (c == '+')? 1 : -1);
                else if (c == '.' || (c >= '0' && c <= '9'))
                    return parse_number(line, i, 0);
                else if (c == '[')
                    return parse_array(line, i, begin, end);
                else if (c == '{')
                    return parse_struct(line, i, begin, end);
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

