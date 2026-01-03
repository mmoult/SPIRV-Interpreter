/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef FRONT_ARGPARSE_HPP
#define FRONT_ARGPARSE_HPP

#include <cassert>
#include <optional>
#include <string>
#include <vector>

#include "../util/trie.hpp"

namespace ArgParse {

struct Option {
    virtual ~Option() = default;
    virtual unsigned getNumArgs() = 0;
    virtual bool handle(const std::string& arg) = 0;
    virtual std::string getArgNames() = 0;
};

struct Flag final : public Option {
    bool enabled = false;

    virtual ~Flag() = default;
    inline unsigned getNumArgs() override {
        enabled = true;
        return 0;
    }
    inline bool handle(const std::string& arg) override {
        assert(false);
        return false;  // flags may not use arguments!
    }
    inline std::string getArgNames() override {
        return "";
    }
};

template<typename T>
class UnaryOption : public Option {
    std::vector<T> values;
    std::string argName;
    bool isSet = false;

protected:
    virtual std::optional<T> isValid(std::string str) = 0;

public:
    UnaryOption(const std::string& arg_name) : argName(arg_name) {}
    UnaryOption(const std::string& arg_name, const T& def_value) : argName(arg_name) {
        values.push_back(def_value);
    }
    virtual ~UnaryOption() = default;

    inline const T& getValue() {
        assert(!values.empty());
        return values.back();
    }
    inline std::vector<T>& getValues() {
        return values;
    }
    inline void setValue(const T& val) {
        values.push_back(val);
        isSet = true;
    }

    inline unsigned getNumArgs() override {
        return 1;
    }
    inline bool handle(const std::string& arg) override {
        if (const auto topt = isValid(arg); topt.has_value()) {
            setValue(*topt);
            return true;
        }
        return false;
    }

    inline std::string getArgNames() override {
        return argName;
    }

    /// @brief Whether the option has been set by parsing
    /// @return true if and only if the value has been set by parsing. Returns false if default value is used.
    inline bool isPresent() const {
        return isSet;
    }
    inline bool hasValue() const {
        return !values.empty();
    }
};

class StringOption final : public UnaryOption<std::string> {
protected:
    inline std::optional<std::string> isValid(std::string str) override {
        return {str};
    }

public:
    explicit StringOption(const std::string& arg_name) : UnaryOption(arg_name) {}
    StringOption(const std::string& arg_name, const std::string& def_value) : UnaryOption(arg_name, def_value) {}
};

class UintOption final : public UnaryOption<unsigned> {
protected:
    std::optional<unsigned> isValid(std::string str) override;

public:
    explicit UintOption(const std::string& arg_name) : UnaryOption(arg_name) {}
    UintOption(const std::string& arg_name, unsigned def_value) : UnaryOption(arg_name, def_value) {}
    virtual ~UintOption() = default;
};

class Parser {

    struct OptionData {
        Option* option;
        std::string fullWord;
        std::string description;
        std::string single;

        OptionData(
            Option* option,
            const std::string& full_word,
            const std::string& description,
            const std::string& single
        )
            : option(option), fullWord(full_word), description(description), single(single) {}
    };

    /// @brief list of all added options
    /// Used for printing help messages and saving arguments to the option
    std::vector<OptionData> options;

    struct PositionalData {
        Option* option;
        std::string description;
        bool mandatory;

        PositionalData(Option* option, const std::string& description, bool mandatory)
            : option(option), description(description), mandatory(mandatory) {}
    };

    std::vector<PositionalData> positionals;

    Trie singles;
    Trie fullWords;

    bool posOnly;

    bool
    handleOption(const std::string& option, Option& opt, const std::string& initial, int* i, char* argv[], int argc);

public:
    void addOption(Option* opt, std::string full_word, const std::string& description, const std::string& single = "");

    inline void addPositional(StringOption* opt, const std::string& description, bool mandatory = true) {
        positionals.emplace_back(opt, description, mandatory);
    }

    bool parse(int argc, char* argv[]);

    void printHelp(unsigned option_len, std::vector<std::string>& intro);
};

};  // namespace ArgParse
#endif
