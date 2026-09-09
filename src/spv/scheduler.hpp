/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_SCHEDULER_HPP
#define SPV_SCHEDULER_HPP

#include <cassert>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

class Scheduler {
    std::set<unsigned> live_threads;
    std::set<unsigned> active_threads;
    unsigned num_invocations;

    struct Token {
        bool special;
        unsigned value;
    };
    std::vector<Token> pattern;
    unsigned pattern_index = 0;
    unsigned next = 0;

public:
    enum Mode : unsigned {
        RANDOM,
        ROUND_ROBIN,
        SEQUENTIAL,
    };
private:
    Mode mode;
    std::mt19937 generator;

public:
    void init(unsigned num_invocations);

    /// @brief Selects the next invocation to run, if any are available.
    /// @return whether all threads were woken up
    bool proceed();

    unsigned getNext() {
        return next;
    }
    void setNext(unsigned to_next) {
        assert(to_next < num_invocations);
        next = to_next;
    }

    /// @brief Returns whether there are any live threads to be run.
    bool hasNext() const {
        return !live_threads.empty();
    }

    bool isValid(unsigned invocation) const {
        return active_threads.contains(invocation);
    }

    /// @brief Marks the given invocation as blocked, so it will not be scheduled until all others have reached it.
    /// @param invocation the index of the invocation to block
    void block(unsigned invocation);

    /// @brief Marks the given invocation as completed, so it will no longer be scheduled.
    /// @param invocation the index of the invocation to complete
    void finish(unsigned invocation);

    unsigned setMode(Mode new_mode, unsigned seed = 0);

    Mode getMode() const {
        return mode;
    }

    bool applyPattern(const std::string& pattern_str);

    std::string toString() const;
};
#endif
