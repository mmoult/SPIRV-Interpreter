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
#include <stdexcept>
#include <vector>

#include "../util/trie.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
export module debug;
import console;
import data;
import format.parse;
import instruction;
import value.string;

struct BreakPoint {
    unsigned lineNo;
    bool ephemeral;
    unsigned hitCount;

    BreakPoint(unsigned line_no, bool ephemeral = false): lineNo(line_no), ephemeral(ephemeral), hitCount(0) {};
};

export class Debugger {
    const std::vector<Spv::Instruction>& insts;
    unsigned maxLineDigits;
    ValueFormat& format;

    // Command handling:
    Trie rootCommands;
    Trie breakCommands;
    Trie progCommands;
    enum Cmd : unsigned {
        BAD, // failure code
        BREAK,
        BREAK_ADD,
        BREAK_CLEAR,
        BREAK_LIST,
        BREAK_REMOVE,
        DISPLAY,
        HELP,
        IN,
        NEXT,
        OUT,
        PROGRAM,
        PROGRAM_ALL,
        PROGRAM_AT,
        QUIT,
        RUN,
        STACK,
    };

    std::vector<BreakPoint> bps;
    bool stopNext = true;  // Halt on the first invocation (to allow user to set breakpoints)
    bool first = true;     // Only print the intro at first invocation

    Cmd process(std::string command) const {
        auto [trie, missing] = rootCommands.next(command);
        if (trie == nullptr) {
            std::cout << "Command \"" << command << "\" is not recognized! Use \"help\" to see options.";
            std::cout << std::endl;
            return Cmd::BAD;
        }
        if (!trie->hasValue()) {
            std::cout << "Ambiguous command \"" << command << missing << "\"";
            if (!missing.empty())
                std::cout << " (assumed from \"" << command << "\")";
            std::cout << "! Cannot decide between: ";
            bool first_command = true;
            for (std::string& opt : trie->enumerate()) {
                if (first_command)
                    std::cout << ", ";
                std::cout << opt;
            }
            std::cout << std::endl;
            return Cmd::BAD;
        }
        return static_cast<Cmd>(trie->getValue());
    }

    std::optional<unsigned> from(std::string s) const {
        int num = std::stoi(s);
        if (num < 0)
            std::cout << "Could not parse positive integer from \"" << s << "\"!" << std::endl;
        return std::optional<unsigned>(static_cast<unsigned>(num));
    }

    /// @brief Calculate the number digits to display the input number
    /// @param num the number to be displayed
    /// @return the number of digits
    unsigned numDigits(unsigned num) const {
        unsigned spaces = 1;
        unsigned power = 10;
        // assumes that the number of lines < UINT_MAX / 10
        for (; num >= power; power *= 10)
            ++spaces;
        return spaces;
    }

    void warnNoArgs(std::string command, const std::vector<std::string>& tokens) const {
        if (tokens.size() > 1)
            std::cout << "\"" << command << "\" takes no arguments!" << std::endl;
    }

public:
    Debugger(const std::vector<Spv::Instruction>& insts, ValueFormat& format): insts(insts), format(format) {
        rootCommands.insert("break", Cmd::BREAK);
        breakCommands.insert("add", Cmd::BREAK_ADD);
        breakCommands.insert("clear", Cmd::BREAK_CLEAR);
        breakCommands.insert("list", Cmd::BREAK_LIST);
        breakCommands.insert("remove", Cmd::BREAK_REMOVE);
        rootCommands.insert("continue", Cmd::RUN);
        rootCommands.insert("display", Cmd::DISPLAY);
        rootCommands.insert("exit", Cmd::QUIT);
        rootCommands.insert("help", Cmd::HELP);
        rootCommands.insert("in", Cmd::IN);
        rootCommands.insert("next", Cmd::NEXT);
        rootCommands.insert("out", Cmd::OUT);
        rootCommands.insert("program", Cmd::PROGRAM);
        progCommands.insert("all", Cmd::PROGRAM_ALL);
        progCommands.insert("at", Cmd::PROGRAM_AT);
        rootCommands.insert("quit", Cmd::QUIT);
        rootCommands.insert("run", Cmd::RUN);
        rootCommands.insert("stack", Cmd::STACK);
        maxLineDigits = numDigits(insts.size());
    }

    void print(unsigned which, const std::vector<Data>& data) const {
        std::stringstream out;
        ValueMap vars;
        std::stringstream result_name;
        result_name << '%' << which;

        const Value* val;
        bool deleteAfter = true;
        std::vector<Type*> to_delete;

        const String empty("null");
        if (const Value* agot = data[which].getValue(); agot != nullptr) {
            val = agot;
            deleteAfter = false;
        } else if (const Type* tgot = data[which].getType(); tgot != nullptr)
            val = tgot->asValue(to_delete);
        else if (const Variable* vgot = data[which].getVariable(); vgot != nullptr)
            val = vgot->asValue(to_delete);
        else if (const Function* fgot = data[which].getFunction(); fgot != nullptr)
            val = fgot->asValue(to_delete);
        else
            val = &empty;
        vars[result_name.str()] = val;

        // Print the result
        format.printFile(out, vars);
        std::cout << out.str() << std::flush;

        // Clean up temporary variables
        if (deleteAfter) {
            delete val;
            for (const Type* t : to_delete)
                delete t;
        }
    }

    void printLine(unsigned i_at) const {
        constexpr unsigned BUFFER = 2;
        std::cout << i_at << std::string(maxLineDigits - numDigits(i_at) + BUFFER, ' ');
        insts[i_at].print();
    }

    bool invoke(unsigned i_at, std::vector<Data>& data) {
        bool stop = stopNext;
        if (stop)
            stopNext = false;
        else {
            for (unsigned i = 0; i < bps.size(); ++i) {
                auto& bp = bps[i];
                if (i_at == bp.lineNo) {
                    stop = true;
                    // If this breakpoint, is ephemeral, remove it
                    if (bp.ephemeral)
                        bps.erase(bps.begin() + i);
                    else
                        bp.hitCount++;
                    break;
                }
            }
        }

        if (first) {
            std::cout << "spirv-run debugger. (enter \"help\" for command options)" << std::endl;
            first = false;
        }
        while (stop) {
            // Prompt the user for entry.
            std::cout << "] ";
            std::string line;
            std::getline(std::cin, line);
            // Commands have no spaces in them. Skip leading spaces (if any), then break on first space
            std::vector<std::string> tokens;
            unsigned start = 0;
            bool started = false;
            for (unsigned i = 0; i < line.length(); ++i) {
                if (line[i] == ' ') {
                    if (!started)
                        continue;
                    // Found the end of the command
                    tokens.push_back(line.substr(start, i));
                    started = false;
                } else if (!started) {
                    start = i;
                    started = true;
                }
            }
            if (started)
                tokens.push_back(line.substr(start));

            if (tokens.empty())
                continue; // empty lines should be ignored

            // Now we run the command through the trie to determine which command was entered
            Cmd entered = process(tokens[0]);

#define CASE case Cmd::
            switch (entered) {
            CASE BAD: // failure code
                continue;  // process will print the error for us. Carry on
            CASE BREAK:
                break;
            CASE BREAK_ADD:
                break;
            CASE BREAK_CLEAR:
                break;
            CASE BREAK_LIST:
                break;
            CASE BREAK_REMOVE:
                break;
            CASE DISPLAY: {
                // Need uint arg for the data to display
                if (tokens.size() < 2) {
                    std::cout << "Missing <data> positive integer!" << std::endl;
                    break;
                } else if (tokens.size() > 2) {
                    std::cout << "Too many arguments given for \"display\"! Only <data> needed." << std::endl;
                    break;
                }
                auto may_which = from(tokens[1]);
                if (!may_which.has_value())
                    break;
                unsigned which = *may_which;
                if (which >= data.size())
                    std::cout << "Cannot display %" << which << "! Outside of data range." << std::endl;
                print(which, data);
                break;
            }
            CASE HELP: {
                // Verify that there are no other tokens
                // (At a future time, we could implement help targeting specific commands)
                warnNoArgs("help", tokens);
                Console console(21);
                console.print("Choose one of the following options:");
                /*
                console.print("Set a breakpoint at the current instruction.", "break");
                console.print("Set a breakpoint at <line>, where <line> is a positive integer.", "  add <line>");
                console.print("Remove all breakpoints.", "  clear");
                console.print("List all breakpoints.", "  list");
                console.print("Remove the breakpoint at the current instruction.", "  remove");
                console.print("Remove the breakpoint at <line>, where <line> is a positive integer.", "    <line>");
                */
                console.print("Print %<data>, where <data> is a positive integer.", "display <data>");
                console.print("Print this help message.", "help");
                console.print("Step into the execution of the current instruction.", "in");
                /*
                console.print("Execute the next instruction.", "next");
                console.print("Execute until the current function is returned from.", "out");
                console.print(
                    "Execute until <fx> functions are returned from, where <fx> is a positive integer",
                    "  <fx>"
                );
                console.print("Print the previous 3 lines, the current line, and the next 3 lines.", "program");
                console.print(
                    "Print the previous <x> lines, the current line, and the next <x> lines, where <x> is a"
                    "positive integer.", "  <x>"
                );
                console.print("Print the whole program", "  all");
                console.print(
                    "Print <line>, the 3 lines before, and 3 lines after, where <line> is a positive integer",
                    "  at <line>"
                );
                console.print(
                    "Print <line>, the <x> lines before, and <x> lines after, where <line> and <x> are positive "
                    "integers.",
                    "    <x>"
                );
                */
                console.print("Quit", "quit / exit");
                console.print("Execute until the next breakpoint", "run / continue");
                /*
                console.print("Execute until <line>, where <line> is a positive integer.", "  <line>");
                console.print("Print current stack information", "stack");
                */
                break;
            }
            CASE IN:
                warnNoArgs("in", tokens);
                stopNext = true;
                stop = false;
                break;
            CASE NEXT:
                break;
            CASE OUT:
                break;
            CASE PROGRAM:
                break;
            CASE PROGRAM_ALL:
                break;
            CASE QUIT:
                warnNoArgs("quit", tokens);
                return true;
            CASE RUN:
                stop = false;
                break;
            CASE STACK:
                break;
            default:
                assert(false); // no other command should be reachable!
                break;
            }
#undef CASE
        }

        return false;  // do not terminate the execution
    }

};
