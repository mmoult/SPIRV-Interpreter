/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../util/trie.hpp"
export module front.argparse;
import front.console;

export namespace ArgParse {

struct Option {
    virtual ~Option() = default;
    virtual unsigned getNumArgs() = 0;
    virtual bool handle(const std::string& arg) = 0;
    virtual std::string getArgNames() = 0;
};

struct Flag : public Option {
    bool enabled = false;

    virtual ~Flag() = default;
    virtual unsigned getNumArgs() override {
        enabled = true;
        return 0;
    }
    virtual bool handle(const std::string& arg) override {
        assert(false);
        return false; // flags may not use arguments!
    }
    virtual std::string getArgNames() override {
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
    UnaryOption(const std::string& arg_name): argName(arg_name) {}
    UnaryOption(const std::string& arg_name, const T& def_value): argName(arg_name) {
        values.push_back(def_value);
    }
    virtual ~UnaryOption() = default;

    const T& getValue() {
        assert(!values.empty());
        return values.back();
    }
    std::vector<T>& getValues() {
        return values;
    }
    void setValue(const T& val) {
        values.push_back(val);
        isSet = true;
    }

    virtual unsigned getNumArgs() override {
        return 1;
    }
    virtual bool handle(const std::string& arg) override {
        if (const auto topt = isValid(arg); topt.has_value()) {
            setValue(*topt);
            return true;
        }
        return false;
    }

    virtual std::string getArgNames() override {
        return argName;
    }

    /// @brief Whether the option has been set by parsing
    /// @return true if and only if the value has been set by parsing. Returns false if default value is used.
    bool isPresent() const {
        return isSet;
    }
    bool hasValue() const {
        return !values.empty();
    }
};

class StringOption : public UnaryOption<std::string> {
protected:
    virtual std::optional<std::string> isValid(std::string str) override {
        return {str};
    }

public:
    explicit StringOption(const std::string& arg_name): UnaryOption(arg_name) {}
    StringOption(const std::string& arg_name, const std::string& def_value): UnaryOption(arg_name, def_value) {}
};

class UintOption : public UnaryOption<unsigned> {
protected:
    virtual std::optional<unsigned> isValid(std::string str) override {
        try {
            int parsed = std::stoi(str, nullptr);
            if (parsed <= 0) {
                std::cerr << "The argument must be > 0, but " << parsed << " was found!";
                return {};
            }
            return {parsed};
        } catch (const std::exception& ex) {
            std::cerr << "Could not parse integer argument! Found string: \"" << str << "\"";
            return {};
        }
    }

public:
    explicit UintOption(const std::string& arg_name): UnaryOption(arg_name) {}
    UintOption(const std::string& arg_name, unsigned def_value): UnaryOption(arg_name, def_value) {}
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
        ): option(option), fullWord(full_word), description(description), single(single) {}
    };

    /// @brief list of all added options
    /// Used for printing help messages and saving arguments to the option
    std::vector<OptionData> options;

    struct PositionalData {
        Option* option;
        std::string description;
        bool mandatory;

        PositionalData(Option* option, const std::string& description, bool mandatory):
            option(option),
            description(description),
            mandatory(mandatory) {}
    };

    std::vector<PositionalData> positionals;

    Trie singles;
    Trie fullWords;

    bool posOnly;

    bool handleOption(
        const std::string& option,
        Option& opt,
        const std::string& initial,
        int* i,
        char* argv[],
        int argc
    ) {
        unsigned args = opt.getNumArgs();
        if (args == 0) {
            if (!initial.empty()) {
                std::cerr << "Cannot pass argument \"" << initial << "\" to option \"" << option;
                std::cerr << "\" without arguments!" << std::endl;
                return false;
            }
            return true;
        }

        std::string arg = initial;
        auto next = [&]() {
            ++*i;
            if (*i >= argc)
                return false;
            arg = std::string(argv[*i]);
            // Check that the next argument fetched isn't a new option
            // - !posOnly: if we only allow positional argument, there can be no flags
            // - arg.empty(): precondition for next. Cannot check content if length is 0
            // - args[0] == '-': if the argument begins with a -, it is likely a flag, not an argument
            // - arg.length() != 1: however, "-" by itself can be an argument, tested by length == 1
            if ((!posOnly && arg.empty()) || (arg[0] == '-' && arg.length() != 1))
                return false;
            return true;
        };

        for (unsigned j = 0; j < args; ++j) {
            if ((j > 0 || arg.empty()) && !next()) {
                std::cerr << "Missing argument for option \"" << option << "\"!" << std::endl;
                return false;
            }

            if (!opt.handle(arg))
                return false;
        }

        return true;
    }

public:

    void addOption(Option* opt, std::string full_word, const std::string& description, const std::string& single = "") {
        options.emplace_back(opt, full_word, description, single);
        unsigned i = options.size() - 1;
        if (!single.empty())
            singles.insert(single, i);
        fullWords.insert(full_word, i);
    }

    void addPositional(StringOption* opt, const std::string& description, bool mandatory = true) {
        positionals.emplace_back(opt, description, mandatory);
    }

    bool parse(int argc, char* argv[]) {
        posOnly = false;
        unsigned pos_at = 0;

        // Remember to skip argv[0] which is the path to the executable
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            unsigned arg_len = arg.length();

            if (!posOnly) {
                if (arg_len > 0 && arg[0] == '-') {
                    std::vector<unsigned> args_to_process;
                    std::string connected = "";

                    if (arg_len > 1 && arg[1] == '-') {
                        if (arg_len == 2) {
                            posOnly = true;
                            continue;
                        }
                        arg = arg.substr(2);

                        // full word option, which may include = for arguments
                        if (std::size_t eq = arg.find('='); eq != std::string::npos) {
                            connected = arg.substr(eq + 1);
                            arg = arg.substr(0, eq);
                            // If we have an explicitly connected argument to the option, we allow leading hyphens
                            if (connected.empty()) {
                                std::cerr << "Missing argument value after = for \"" << arg << "\"!" << std::endl;
                                return false;
                            }
                        }

                        auto [trie, missing] = fullWords.next(arg);
                        if (trie != nullptr) {
                            if (!trie->hasValue()) {
                                std::cerr << "Ambiguous option name \"" << arg << "\"! Cannot decide between: ";
                                bool first_command = true;
                                arg += missing;
                                for (std::string& opt : trie->enumerate()) {
                                    if (first_command)
                                        first_command = false;
                                    else
                                        std::cerr << ", ";
                                    std::cerr << arg << opt;
                                }
                                std::cerr << std::endl;
                            } else
                                args_to_process.push_back(trie->getValue());
                        }
                    } else {
                        // single letter options, may have several grouped
                        for (unsigned j = 1; j < arg_len; ++j) {
                            std::string single_flag = arg.substr(j, 1);

                            auto [trie, missing] = singles.next(single_flag);
                            if (trie != nullptr && trie->hasValue())
                                args_to_process.push_back(trie->getValue());
                            else {
                                arg = single_flag;
                                args_to_process.clear();  // force an error on the unknown flag name
                                break;
                            }
                        }
                    }

                    if (args_to_process.empty()) {
                        std::cerr << "No option name matches input \"" << arg << "\"!" << std::endl;
                        return false;
                    }

                    for (unsigned arg_i : args_to_process) {
                        if (!handleOption(arg, *options[arg_i].option, connected, &i, argv, argc))
                            return false;
                    }
                    continue;
                }
                // fallthrough when not flag option
            }

            if (pos_at < positionals.size()) {
                const auto& pos_data = positionals[pos_at];
                if (!handleOption(pos_data.description, *pos_data.option, arg, &i, argv, argc))
                    return false;
                ++pos_at;
            } else {
                std::cerr << "Unexpected option or argument \"" << arg << "\"!" << std::endl;
                return false;
            }
        }

        bool ok = true;
        for (; pos_at < positionals.size(); ++pos_at) {
            const auto& pos_data = positionals[pos_at];
            if (pos_data.mandatory) {
                std::cerr << "Missing positional argument: " << pos_data.description << std::endl;
                ok = false;  // continue to print until all positionals get their errors out
            }
        }
        return ok;
    }

    void printHelp(unsigned option_len, std::vector<std::string>& intro) {
        Console console(option_len);
        for (const auto& s : intro)
            console.print(s);

        for (const auto& option_data : options) {
            std::stringstream header;
            if (!option_data.single.empty())
                header << '-' << option_data.single << " / ";
            header << "--" << option_data.fullWord;
            if (std::string arg_names = option_data.option->getArgNames(); !arg_names.empty())
                header << ' ' << arg_names;

            console.print(option_data.description, header.str());
        }
    }
};

};
