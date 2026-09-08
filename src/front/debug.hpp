/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef FRONT_DEBUG_HPP
#define FRONT_DEBUG_HPP

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../format/parse.hpp"
#include "../spv/data/manager.hpp"
#include "../spv/frame.hpp"
#include "../spv/inst-list.hpp"
#include "../spv/invocation.hpp"
#include "../spv/scheduler.hpp"
#include "../util/trie.hpp"

struct BreakPoint {
    unsigned hitCount = 0;
};

class Debugger {
    const InstList& insts;
    ValueFormat& format;
    unsigned maxLineDigits;
    unsigned maxInvocDigits;
    static constexpr unsigned BUFFER = 2;

    // Command handling:
    Trie rootCommands;
    Trie breakCommands;
    Trie invocCommands;
    Trie progCommands;
    Trie schedCommands;

    enum Cmd : unsigned {
        BAD,  // failure code
        ADD,
        ALL,
        AT,
        BREAK,
        CLEAR,
        CUSTOM,
        DISPLAY,
        HELP,
        INVOCATION,
        LIST,
        NEXT,
        PROGRAM,
        QUIT,
        RANDOM,
        REMOVE,
        RETURN,
        ROUND,
        RUN,
        SCHEDULE,
        SEQUENTIAL,
        SHOW,
        STACK,
        STEP,
    };

    bool first = true;  // Only print the intro at first invocation
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

    Cmd process(std::string command, Trie& options) const;

    std::optional<unsigned> from(std::string s, bool quiet = false) const;

    /// @brief Calculate the number digits to display the input number
    /// @param num the number to be displayed
    /// @return the number of digits
    inline unsigned numDigits(unsigned num) const {
        unsigned spaces = 1;
        unsigned power = 10;
        // assumes that the number of lines < UINT_MAX / 10
        for (; num >= power; power *= 10)
            ++spaces;
        return spaces;
    }

    inline void warnNoArgs(std::string command, const std::vector<std::string>& tokens) const {
        if (tokens.size() > 1)
            std::cout << "\"" << command << "\" takes no arguments!" << std::endl;
    }
    void warnExtraArgs(std::string command, unsigned args, unsigned expected) const;

    inline void breakOnReturn(const std::vector<Frame*>& frame_stack) {
        // Create an ephemeral at the pc of the frame below ours on the stack
        if (frame_stack.size() > 1) {
            const Frame* frame = frame_stack[frame_stack.size() - 2];
            ephemeral.on = true;
            ephemeral.line = frame->getPC() + 1;
        }
    }

public:
    Debugger(const InstList& insts, ValueFormat& format, unsigned num_invoc);

    void print(unsigned which, const DataView& data) const;

    inline void printLine(unsigned invoc, unsigned i_at) const {
        if (maxInvocDigits > 0)
            std::cout << "I" << invoc << std::string(maxInvocDigits - numDigits(invoc) + BUFFER, ' ');
        std::cout << i_at << std::string(maxLineDigits - numDigits(i_at) + BUFFER, ' ');
        insts[i_at].print();
    }

    enum class Action {
        NONE,
        RETRY,
        QUIT,
    };

    /// @brief Invoke the debugger for the current invocation
    /// @param threads the state of all invocation threads
    /// @param invocation the index of the current invocation
    /// @param scheduler the program scheduler
    /// @return whether the debugger has requested to terminate execution
    Action invoke(const std::vector<Invocation>& threads, unsigned& invocation, Scheduler& scheduler);

    void suggestStop() {
        stopNext = true;
    }
};
#endif
