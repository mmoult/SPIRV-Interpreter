/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>  // max
#include <cassert>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

#include "../spv/data/manager.h"
#include "../util/trie.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
export module front.debug;
import format.parse;
import front.console;
import spv.data.data;
import spv.frame;
import spv.instruction;
import value.string;

struct BreakPoint {
    unsigned hitCount = 0;
};

export class Debugger {
    const std::vector<Instruction>& insts;
    unsigned maxLineDigits;
    ValueFormat& format;
    static constexpr unsigned BUFFER = 2;

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
        NEXT,
        PROGRAM,
        PROGRAM_ALL,
        PROGRAM_AT,
        QUIT,
        RUN,
        RETURN,
        STACK,
        STEP,
    };

    bool first = true;     // Only print the intro at first invocation
    std::map<unsigned, BreakPoint> bps;
    bool stopNext = true;  // Halt on the first invocation (to allow user to set breakpoints)
    struct {
        bool on = false;
        unsigned frame;
    } nextCheck;
    struct {
        bool on = false;
        unsigned line;
    } ephemeral;

    Cmd process(std::string command, Trie& options) const {
        auto [trie, missing] = options.next(command);
        if (trie == nullptr) {
            std::cout << "Command \"" << command << "\" is not recognized! Use \"help\" to see options.";
            std::cout << std::endl;
            return Cmd::BAD;
        }
        if (!trie->hasValue()) {
            std::cout << "Ambiguous command \"" << command << "\"";
            std::cout << "! Cannot decide between: ";
            bool first_command = true;
            command += missing;
            for (std::string& opt : trie->enumerate()) {
                if (first_command)
                    first_command = false;
                else
                    std::cout << ", ";
                std::cout << command << opt;
            }
            std::cout << std::endl;
            return Cmd::BAD;
        }
        return static_cast<Cmd>(trie->getValue());
    }

    std::optional<unsigned> from(std::string s, bool quiet = false) const {
        int num;
        try {
            num = std::stoi(s);
        } catch (...) {
            if (!quiet)
                std::cout << "Could not parse nonnegative integer from \"" << s << "\"!" << std::endl;
            return {};
        }
        if (num < 0) {
            if (!quiet)
                std::cout << "Need a nonnegative integer, but got \"" << num << "\" instead!" << std::endl;
            return {};
        }
        return {static_cast<unsigned>(num)};
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
    void warnExtraArgs(std::string command, unsigned args, unsigned expected) const {
        if (args > expected) {
            std::cout << "\"" << command << "\" expects " << expected << " argument";
            if (expected != 1)
                std::cout << "s";
            std::cout << ", but " << args << " argument";
            if (args != 1)
                std::cout << "s were";
            else
                std::cout << "was";
            std::cout << " given!";
        }
    }

    void breakOnReturn(const std::vector<Frame*>& frame_stack) {
        // Create an ephemeral at the pc of the frame below ours on the stack
        if (frame_stack.size() > 1) {
            const Frame* frame = frame_stack[frame_stack.size() - 2];
            ephemeral.on = true;
            ephemeral.line = frame->getPC() + 1;
        }
    }

public:
    Debugger(const std::vector<Instruction>& insts, ValueFormat& format): insts(insts), format(format) {
        rootCommands.insert("break", Cmd::BREAK);
        breakCommands.insert("add", Cmd::BREAK_ADD);
        breakCommands.insert("clear", Cmd::BREAK_CLEAR);
        breakCommands.insert("list", Cmd::BREAK_LIST);
        breakCommands.insert("remove", Cmd::BREAK_REMOVE);
        rootCommands.insert("continue", Cmd::RUN);
        rootCommands.insert("display", Cmd::DISPLAY);
        rootCommands.insert("exit", Cmd::QUIT);
        rootCommands.insert("help", Cmd::HELP);
        rootCommands.insert("next", Cmd::NEXT);
        rootCommands.insert("program", Cmd::PROGRAM);
        progCommands.insert("all", Cmd::PROGRAM_ALL);
        progCommands.insert("at", Cmd::PROGRAM_AT);
        rootCommands.insert("quit", Cmd::QUIT);
        rootCommands.insert("return", Cmd::RETURN);
        rootCommands.insert("run", Cmd::RUN);
        rootCommands.insert("stack", Cmd::STACK);
        rootCommands.insert("step", Cmd::STEP);
        maxLineDigits = numDigits(insts.size());
    }

    void print(unsigned which, const DataView& data) const {
        std::stringstream out;
        ValueMap vars;
        std::stringstream result_name;
        result_name << '%' << which;

        const Value* val;
        bool deleteAfter = true;
        std::vector<Type*> to_delete;

        const String empty("null");
        bool defaulted = false;
        if (!data.contains(which))
            defaulted = true;
        else {
            const Data& dat = data[which];
            if (const Value* agot = dat.getValue(); agot != nullptr) {
                val = agot;
                deleteAfter = false;
            } else if (const Type* tgot = dat.getType(); tgot != nullptr)
                val = tgot->asValue(to_delete);
            else if (const Variable* vgot = dat.getVariable(); vgot != nullptr)
                val = vgot->asValue(to_delete);
            else if (const Function* fgot = dat.getFunction(); fgot != nullptr)
                val = fgot->asValue(to_delete);
            else
                defaulted = true;
        }
        if (defaulted) {
            val = &empty;
            deleteAfter = false;
        }

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
        std::cout << i_at << std::string(maxLineDigits - numDigits(i_at) + BUFFER, ' ');
        insts[i_at].print();
    }

    bool invoke(unsigned i_at, const DataView& data, const std::vector<Frame*>& frame_stack) {
        bool stop = stopNext;
        if (stop) {
            if (nextCheck.on) {
                // If we are at the same frame as before, we have stopped on next successfully
                // Otherwise, we need to continue until the return back to the previous stack frame.
                if (nextCheck.frame < frame_stack.size()) {
                    stop = false;
                    breakOnReturn(frame_stack);
                }
                nextCheck.on = false;
            }
            stopNext = false;
        }
        // Cannot do an else because flow may need to fall through when stop set to false
        if (!stop) {
            // Compare against breakpoints and the ephemeral breakpoint
            if (bps.contains(i_at)) {
                auto& bp = bps[i_at];
                stop = true;
                bp.hitCount++;
                // Any stop forgets the ephemeral
                ephemeral.on = false;
            } else if (ephemeral.on) {
                if (i_at == ephemeral.line) {
                    stop = true;
                    ephemeral.on = false;
                }
            }
        }

        if (first && stop) {
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
                    tokens.push_back(line.substr(start, i - start));
                    started = false;
                } else if (!started) {
                    start = i;
                    started = true;
                }
            }
            if (started)
                tokens.push_back(line.substr(start));

            if (tokens.empty())
                continue;  // empty lines should be ignored

            // Now we run the command through the trie to determine which command was entered
#define CASE case Cmd::
            switch (process(tokens[0], rootCommands)) {
            CASE BAD:
                continue;  // process will print the error for us. Carry on
            CASE HELP: {
                // Verify that there are no other tokens
                // (At a future time, we could implement help targeting specific commands)
                warnNoArgs("help", tokens);
                Console console(21);
                console.print("Choose one of the following options:");
                console.print("Toggle a breakpoint at the current instruction.", "break");
                console.print("Set a breakpoint at <line>, where <line> is a nonnegative integer.", "  add <line>");
                console.print("Remove all breakpoints.", "  clear");
                console.print("List all breakpoints.", "  list");
                console.print(
                    "Remove the breakpoint at <line>, where <line> is a nonnegative integer.",
                    "  remove <line>"
                );
                console.print("Print %<data>, where <data> is a nonnegative integer.", "display <data>");
                console.print("Print this help message.", "help");
                console.print("Execute the next instruction in this function, stepping over any calls.", "next");
                console.print("Print the previous 3 lines, the current line, and the next 3 lines.", "program");
                console.print(
                    "Print the previous <x> lines, the current line, and the next <x> lines, where <x> is a "
                    "nonnegative integer.", "  <x>"
                );
                console.print("Print the whole program", "  all");
                console.print(
                    "Print <line>, the 3 lines before, and 3 lines after, where <line> is a nonnegative integer",
                    "  at <line>"
                );
                console.print(
                    "Print <line>, the <x> lines before, and <x> lines after, where <line> and <x> are nonnegative "
                    "integers.",
                    "    <x>"
                );
                console.print("Quit", "quit / exit");
                console.print("Execute until the next breakpoint", "run / continue");
                console.print("Execute until <line>, where <line> is a nonnegative integer.", "  <line>");
                console.print("Execute until the current function is returned from.", "return");
                console.print("Step to the next instruction, going into any functions called.", "step");
                console.print("Print current stack information", "stack");
                break;
            }
            CASE BREAK: {
                if (tokens.size() == 1) {
                    // Toggle the breakpoint here
                    if (bps.contains(i_at))
                        bps.erase(i_at);
                    else
                        bps.emplace(i_at, BreakPoint());
                    break;
                }
                switch (process(tokens[1], breakCommands)) {
                CASE BAD:
                    break;
                CASE BREAK_ADD: {
                    if (tokens.size() < 3) {
                        std::cout << "Missing <line> argument for command \"break add\"!" << std::endl;
                        break;
                    }
                    auto found = from(tokens[2]);
                    if (!found.has_value())
                        break;  // error already printed
                    warnExtraArgs("break add", 1, tokens.size() - 2);
                    unsigned line_no = *found;
                    if (bps.contains(line_no)) {
                        std::cout << "There already exists a breakpoint at line " << line_no << "!" << std::endl;
                        break;
                    }
                    bps.emplace(line_no, BreakPoint());
                    break;
                }
                CASE BREAK_CLEAR:
                    warnExtraArgs("break clear", 0, tokens.size() - 2);
                    bps.clear();
                    break;
                CASE BREAK_LIST: {
                    warnExtraArgs("break list", 0, tokens.size() - 2);
                    if (bps.empty()) {
                        std::cout << "no breakpoints..." << std::endl;
                        break;
                    }
                    const std::string line_header = "line:";
                    unsigned max_line = 0;
                    for (auto const& pair : bps)
                        max_line = std::max(max_line, pair.first);
                    max_line = std::max(static_cast<unsigned>(line_header.length()), numDigits(max_line)) + BUFFER;

                    std::cout << line_header << std::string(max_line - line_header.length(), ' ');
                    std::cout << "hits:" << std::endl;
                    for (auto const& [line_no, bp] : bps) {
                        unsigned line_len = numDigits(line_no);
                        std::cout << line_no << std::string(max_line - line_len, ' ') << bp.hitCount << std::endl;
                    }
                    break;
                }
                CASE BREAK_REMOVE: {
                    if (tokens.size() < 3) {
                        std::cout << "Missing <line> argument for command \"break remove\"!" << std::endl;
                        break;
                    }
                    auto found = from(tokens[2]);
                    if (!found.has_value())
                        break;  // error already printed
                    warnExtraArgs("break remove", 1, tokens.size() - 2);
                    unsigned line_no = *found;
                    if (!bps.contains(line_no)) {
                        std::cout << "There is no breakpoint to remove from line " << line_no << "!" << std::endl;
                        break;
                    }
                    bps.erase(line_no);
                    break;
                }
                default:
                    assert(false);
                    break;
                }
                break;
            }
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
                if (which >= data.getBound() || which == 0)
                    std::cout << "Cannot display %" << which << "! Outside of data range." << std::endl;
                else
                    print(which, data);
                break;
            }
            CASE NEXT:
                warnNoArgs("next", tokens);
                stopNext = true;
                nextCheck.on = true;
                nextCheck.frame = frame_stack.size();
                stop = false;
                break;
            CASE PROGRAM: {
                unsigned line_print = frame_stack.back()->getPC();
                unsigned surround = 3;
                bool all = false;
                unsigned num_tokens = tokens.size();
                if (num_tokens > 1) {
                    auto found = from(tokens[1], true);
                    if (found.has_value())
                        surround = *found;
                    else {
                        switch (process(tokens[1], progCommands)) {
                        CASE BAD:
                            continue;
                        CASE PROGRAM_ALL:
                            warnExtraArgs("program all", 0, num_tokens - 2);
                            all = true;
                            break;
                        CASE PROGRAM_AT: {
                            if (num_tokens < 2) {
                                std::cout << "Missing <line> argument for command \"program at\"!" << std::endl;
                                break;
                            } else {
                                auto lfound = from(tokens[2]);
                                if (!lfound.has_value())
                                    break;
                                line_print = *lfound;
                                if (num_tokens > 2) {
                                    auto xfound = from(tokens[3]);
                                    if (!xfound.has_value())
                                        break;
                                    warnExtraArgs("program at", 2, num_tokens - 3);
                                    surround = *xfound;
                                }
                            }
                            break;
                        }
                        default:
                            assert(false);
                            break;
                        }
                    }
                }

                unsigned start, end;
                if (all) {
                    start = 0;
                    end = insts.size();
                } else {
                    unsigned inst_max = insts.size();
                    start = std::min(inst_max - 1, line_print);
                    if (surround > start)
                        start = 0;
                    else
                        start -= surround;
                    end = std::min(line_print, inst_max);
                    // To prevent overflow, we cannot add to end arbitrarily. Because of the previous min, we know that
                    // end <= inst_max
                    if (unsigned diff = inst_max - end; diff > 0)
                        end += std::min(diff, surround + 1);
                }
                for (unsigned i = start; i < end; ++i) {
                    bool bp = bps.contains(i);
                    bool curr = (i == i_at);

                    // Print special line annotations
                    if (bp)
                        std::cout << '+';
                    else if (curr)
                        std::cout << '>';
                    else
                        std::cout << ' ';
                    if (curr)
                        std::cout << '>';
                    else
                        std::cout << ' ';

                    std::cout << i << std::string(maxLineDigits - numDigits(i) + BUFFER, ' ');
                    insts[i].print();
                }
                break;
            }
            CASE QUIT:
                warnNoArgs("quit", tokens);
                return true;
            CASE RETURN:
                warnNoArgs("return", tokens);
                breakOnReturn(frame_stack);
                stop = false;
                break;
            CASE RUN:
                if (tokens.size() > 1) {
                    // Specify a specific line to stop on
                    auto found = from(tokens[1]);
                    if (!found.has_value())
                        break;
                    warnExtraArgs("run", 1, tokens.size() - 1);
                    ephemeral.on = true;
                    ephemeral.line = *found;
                }
                stop = false;
                break;
            CASE STACK: {
                warnNoArgs("stack", tokens);
                const std::string pc_label = "pc:";
                const std::string return_label = "return:";
                unsigned pc_max = 0;
                unsigned return_max = 0;
                for (const auto* frame : frame_stack) {
                    pc_max = std::max(pc_max, frame->getPC());
                    return_max = std::max(return_max, frame->getReturn());
                }
                pc_max = std::max(static_cast<unsigned>(pc_label.length()), numDigits(pc_max)) + BUFFER;
                return_max = std::max(static_cast<unsigned>(return_label.length()), numDigits(return_max)) + BUFFER;

                std::cout << pc_label << std::string(pc_max - pc_label.length(), ' ');
                std::cout << return_label << std::string(return_max - return_label.length(), ' ');
                std::cout << "last_label:" << std::endl;
                // Print stack frames backwards so the current frame is on top and previous frames are below it
                for (unsigned i = frame_stack.size(); i-- > 0;) {
                    const auto& frame = *frame_stack[i];
                    unsigned pc = frame.getPC();
                    std::cout << pc << std::string(pc_max - numDigits(pc), ' ');
                    if (!frame.hasReturn())
                        std::cout << '-' << std::string(return_max - 1, ' ');
                    else {
                        unsigned ret = frame.getReturn();
                        std::cout << ret << std::string(return_max - numDigits(ret), ' ');
                    }
                    std::cout << '%' << frame.getLabel() << std::endl;
                }
                break;
            }
            CASE STEP:
                warnNoArgs("step", tokens);
                stopNext = true;
                stop = false;
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
