/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "string.hpp"

#include <algorithm>  // for max
#include <cassert>
#include <cmath>
#include <vector>

std::string repeated_string(const unsigned num, const std::string& str) {
    std::stringstream out;
    for (unsigned i = 0; i < num; ++i)
        out << str;
    return out.str();
}

void print_float(std::stringstream& out, double fp) {
    assert(!std::isnan(fp));
    assert(!std::isinf(fp));
    // The number of precision digits to print. A float has roughly 5 digits after the first nonzero. Print through
    // either 1 digit after the decimal or prec_rem after the first relevant digit, whichever comes *last*.
    constexpr unsigned PRECISION_DIGITS = 6;
    // The number of 0s before we use scientific notation for subnormals.
    constexpr unsigned DIGITS_TO_SCIENTIFIC = 5;

    if (std::signbit(fp)) {
        fp = std::abs(fp);
        out << "-";
    }

    // Converted digit characters. Does *not* include the decimal point character. Thus, each must be 0..=9
    std::vector<char> digits;
    // The decimal comes after this character index. For example:
    //   digits = {0, 0}
    //   dec_idx = 0
    // => 0.0
    unsigned dec_idx = 0;

    double digit = 1.0;
    // Multiply digit by 10 until it is at least the current float divided by 10
    while (true) {
        if (digit > fp / 10.0)
            break;
        digit *= 10.0;
        ++dec_idx;
    }
    unsigned scientific = 0;

    unsigned max = std::max(dec_idx + 1, PRECISION_DIGITS);
    bool prec_start = false;
    for (;; digit /= 10.0) {
        for (unsigned ii = 1; ii < 11; ++ii) {
            if ((ii * digit > fp) || (ii == 10)) {
                unsigned i = ii - 1;
                fp -= (i * digit);

                if (prec_start || i > 0) {
                    prec_start = true;
                    digits.push_back('0' + i);
                } else
                    ++scientific;
                break;
            }
        }
        if (fp == 0.0) {
            if (digits.empty())
                digits.push_back('0');
            break;
        }

        if (digits.size() >= max) {
            // Done creating new characters!

            // Check if we should round up from the remaining value
            if (fp >= (digit / 2.0)) {
                for (unsigned at = 1;; ++at) {
                    if (at > digits.size()) {
                        // Rounding went beyond the initial character, eg: 9.999995 -> 10.00000. Insert a leading 1.
                        // Definitionally, if we reached this point, all characters are 0s. Therefore, we can "push
                        // front" by replacing the first character and replicating a 0 in back
                        digits[0] = '1';
                        digits.push_back('0');
                        if (scientific > 0)
                            --scientific;
                        else
                            ++dec_idx;
                    } else {
                        unsigned i = digits.size() - at;
                        if (digits[i] == '9') {
                            digits[i] = '0';
                            continue;
                        }
                        ++digits[i];
                    }
                    break;
                }
            }
            break;
        }
    }
    // We cannot use both scientific and dec_idx at the same time. If either is nonzero, the other must be zero.
    assert((scientific == 0) || (dec_idx == 0));

    // Determine the print mode
    bool sci_enabled = scientific >= DIGITS_TO_SCIENTIFIC;
    bool regular;
    if (!sci_enabled && scientific > 0) {
        out << "0.";
        if (scientific > 1)
            out << std::string(scientific - 1, '0');
        regular = false;
    } else
        regular = true;

    // Truncate trailing zeros
    max = digits.size() - 1;
    for (; max > 0; --max) {
        if (digits[max] != '0') {
            break;
        }
    }
    ++max;  // exiting indicates the character at index max should be printed
    assert(max >= 1);

    // Print characters collected as final output
    for (unsigned i = 0; i < max; ++i) {
        out << digits[i];
        if (i == dec_idx && regular)
            out << '.';
    }
    if (regular) {
        // Need at minimum 1 character after the decimal AND 2 total
        if (max <= dec_idx)
            out << std::string(dec_idx - max + 1, '0') << '.';
        if ((max <= dec_idx + 1) || (max < 2))
            out << '0';

        if (sci_enabled)
            out << "E-" << scientific;
    }
}