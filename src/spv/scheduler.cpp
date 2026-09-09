/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "scheduler.hpp"

#include <cassert>
#include <sstream>
#include <string>

void Scheduler::init(unsigned num) {
    num_invocations = num;
    // All threads start as live and active
    for (unsigned i = 0; i < num_invocations; ++i) {
        live_threads.insert(i);
        active_threads.insert(i);
    }

    if (mode == Mode::ROUND_ROBIN)
        next = live_threads.size();
}

unsigned Scheduler::setMode(Mode new_mode, unsigned seed) {
    mode = new_mode;
    if (mode == Mode::RANDOM) {
        // If not specified, choose the seed from hardware randomness.
        while (seed == 0) {
            // Choose a seed from hardware randomness, if available
            std::random_device rd;
            seed = rd();
        }
        generator.seed(seed);
    } else if (mode == Mode::ROUND_ROBIN) {
        if (!live_threads.empty())
            next = live_threads.size();
    } else if (mode == Mode::SEQUENTIAL)
        next = 0;
    return seed;
}

bool Scheduler::proceed() {
    bool wake_all = false;
    // Check if all active threads have hit a barrier. If so, unblock.
    if (active_threads.empty()) {
        active_threads.insert(live_threads.begin(), live_threads.end());
        wake_all = true;
    }

    // Select the next live thread to run
    if (!pattern.empty()) {
        // TODO
    }

    // If there is no pattern, select by the mode
    if (mode == Mode::RANDOM) {
        unsigned size = active_threads.size();
        std::uniform_int_distribution<unsigned> distribution(0, size - 1);
        unsigned chosen = distribution(generator);

        unsigned i = 0;
        for (auto it = active_threads.begin(); it != active_threads.end(); ++it) {
            [[unlikely]]
            if (i == chosen) {
                next = *it;
                return wake_all;
            }
            ++i;
        }
        assert(false);
    } else if (mode == Mode::SEQUENTIAL) {
        if (active_threads.contains(next))
            return wake_all;
        // Intentional fallthrough
    }

    // ROUND_ROBIN
    ++next;
    while (!active_threads.contains(next)) {
        if (next >= num_invocations)
            next = 0;
        else
            ++next;
    }

    return wake_all;
}

void Scheduler::block(unsigned invocation) {
    active_threads.erase(invocation);
}

void Scheduler::finish(unsigned invocation) {
    block(invocation);
    live_threads.erase(invocation);
}

bool Scheduler::applyPattern(const std::string& pattern_str) {
    // TODO implement!
    return false;
}

static std::string mode_to_string(Scheduler::Mode mode) {
    switch (mode) {
    case Scheduler::Mode::RANDOM:
        return "random";
    case Scheduler::Mode::ROUND_ROBIN:
        return "round-robin";
    case Scheduler::Mode::SEQUENTIAL:
        return "sequential";
    default:
        assert(false);
        return "invalid!";
    }
}

std::string Scheduler::toString() const {
    std::stringstream out;
    out << "next: " << next << std::endl;
    if (!pattern.empty()) {
        out << "custom: " << std::endl;
        out << "fallback mode: ";
    } else
        out << "mode: ";
    out << mode_to_string(mode);
    return out.str();
}
