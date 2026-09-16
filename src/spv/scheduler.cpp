/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "scheduler.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <sstream>
#include <string>
#include <vector>

#include "../util/string.hpp"

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
    while (!pattern.empty()) {
        next = pattern[pattern.size() - (1 + pattern_index)].value;
        ++pattern_index;

        bool ok = active_threads.contains(next);
        if (pattern_index >= pattern.size()) {
            pattern.clear();
            if (ok)
                return wake_all;
            break;
        } else {
            unsigned index = pattern.size() - (1 + pattern_index);
            if (ok) {
                if (pattern[index].special)
                    pattern_index = 0;
                return wake_all;
            } else {
                // Find the end of the clause. If there is a terminal, remove this from the pattern
                unsigned i = index + 1;
                bool term_found = false;
                for (; i-- > 0;) {
                    if (pattern[i].special) {
                        term_found = true;
                        break;
                    }
                }
                if (term_found) {
                    // Delete the instance which gave us next
                    pattern.erase(pattern.begin() + (index + 1));
                    --pattern_index;
                    // If we have removed all from this clause but its terminal, clear that, too.
                    if (pattern.back().special)
                        pattern.pop_back();
                }
            }
        }
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
    // Specify invocations by their unsigned integer index. Separate with comma:
    //  1 = step invocation 1
    //  3,7 = step 3 then step 7
    // Dot loops execution before (until the last dot) until completion:
    //   0. = 0 is stepped to completion
    //   1.2. = 1 is stepped to completion, then 2 is stepped to completion
    //   1,2. = loop of single steps for 1 and 2 until both finish
    // Ranges may be of comma or dots. They expand to a list of their respective types:
    //   1:,3 = 1,2,3
    //   1:.3 = 1.2.3
    //   5:.2 = 5.4.3.2
    // Each range may contain an optional step value:
    //   0:,5:2 = 0,2,4
    //   7:.5:1 = 7.6.5
    std::vector<Token> parsed;

    std::vector<unsigned> numbers;
    enum RangeType {
        NONE,
        COMMA,
        DOT,
    };
    auto range_type = RangeType::NONE;
    bool need_number = true;

    auto construct_range = [&](int start, int stop, int step = 1) {
        assert(range_type != RangeType::NONE);
        assert(step > 0);
        bool reverse = stop < start;
        if (reverse) {
            start *= -1;
            stop *= -1;
        }
        for (int i = start; i <= stop; i += step) {
            if (i > start && range_type == RangeType::DOT)
                parsed.emplace_back(true, 0);
            unsigned val = static_cast<unsigned>(reverse? -i : i);
            parsed.emplace_back(false, val);
        }
        range_type = RangeType::NONE;
    };

    for (unsigned i = 0; i < pattern_str.length(); ++i) {
        char c = pattern_str[i];
        if (c == ' ')
            continue;

        if (need_number) {
            if (c < '0' || c > '9')
                return false;
            auto* start = pattern_str.data() + i++;
            for (; i < pattern_str.length(); ++i) {
                c = pattern_str[i];
                if (c < '0' || c > '9')
                    break;
            }
            auto end = pattern_str.data() + i--;
            unsigned num;
            auto [ptr, ec] = std::from_chars(start, end, num);
            if (ec != std::errc() || ptr != end)
                return false;
            numbers.push_back(num);
        } else {
            // Observe punctuation
            unsigned sz = numbers.size();
            if (c == '.' || c == ',') {
                if (sz == 1)
                    parsed.emplace_back(false, numbers[0]);
                else if (sz == 2)
                    construct_range(numbers[0], numbers[1]);
                else {
                    assert(sz == 3);
                    construct_range(numbers[0], numbers[1], numbers[2]);
                }
                numbers.clear();
                if (c == '.')
                    parsed.emplace_back(true, 0);
            } else if (c == ':') {
                if (sz == 1) {
                    // Expect an indicator for the type of range
                    ++i;
                    if (i >= pattern_str.length())
                        return false;
                    c = pattern_str[i];
                    if (c == '.')
                        range_type = RangeType::DOT;
                    else if (c == ',')
                        range_type = RangeType::COMMA;
                    else
                        return false;
                } else if (sz == 3)
                    return false;
            } else
                return false;
        }

        need_number = !need_number;
    }

    auto sz = numbers.size();
    if (sz == 1) {
        if (range_type != RangeType::NONE)
            return false;
        parsed.emplace_back(false, numbers[0]);
    } else if (sz == 2)
        construct_range(numbers[0], numbers[1]);
    else if (sz >= 3) {
        assert(sz == 3);
        if (numbers[2] == 0)
            return false;
        construct_range(numbers[0], numbers[1], numbers[2]);
    } else if (parsed.empty())
        return false;

    // Pattern is stored backwards
    std::reverse_copy(parsed.begin(), parsed.end(), std::back_inserter(pattern));
    pattern_index = 0;

    return true;
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
    auto index = pattern.size() - (1 + pattern_index);
    if (!pattern.empty()) {
        out << "custom: ";
        unsigned spaces = 0;
        bool need_sep = false;
        bool placed_marker = false;
        for (unsigned i = pattern.size(); i-- > 0;) {
            const auto& token = pattern[i];
            if (token.special) {
                need_sep = false;
                out << ".";
                if (!placed_marker)
                    ++spaces;
            } else {
                if (need_sep) {
                    out << ",";
                    if (!placed_marker)
                        ++spaces;
                }
                out << token.value;
                need_sep = true;
                if (!placed_marker)
                    spaces += Str::width(token.value);
            }
            if (!placed_marker && i == index)
                placed_marker = true;
        }
        //                  "custom: "
        out << std::endl << "       " << std::string(spaces, ' ') << '^' << std::endl;
        out << "fallback mode: ";
    } else
        out << "mode: ";
    out << mode_to_string(mode);
    return out.str();
}
