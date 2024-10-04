/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cmath>
#include <string>
#include <sstream>
#include <vector>
export module util.string;

/// @brief Get a repeated string.
/// @param num number of times to repeat a string.
/// @param str string to repeat.
/// @return single string of the repeated string.
export std::string repeated_string(const unsigned num, const std::string& str) {
    std::stringstream out;
    for (unsigned i = 0; i < num; ++i)
        out << str;
    return out.str();
}

/// @brief Force regular behavior for printing of floats.
/// Regular streaming is prone to truncation (note: not rounding), which confuses the interpreter's equivalence
/// algorithm.
/// @param out where to print the string representation of float, fp
/// @param fp the float to print. Should not be NaN or Inf!
export void print_float(std::stringstream& out, float fp) {
    if (std::signbit(fp)) {
        fp = std::abs(fp);
        out << "-";
    }

    float digit = 1e-4;
    unsigned scientific = (fp <= digit && fp > 0.0)? 4 : 0;
    if (scientific == 0) {
        digit = 1.0;
        // Multiply digit by 10 until it is at least the current float divided by 10
        while (true) {
            if (digit > fp / 10.0)
                break;
            digit *= 10.0;
        }
    } else {
        // similarly to the non-scientific case, work down until we find where the digits begin
        while (digit > fp) {
            digit /= 10.0;
            ++scientific;
        }
    }

    unsigned precStart = 6;
    unsigned precRem = precStart;  // the number of precision digits to print. Don't go beyond 6 after decimal
    std::vector<char> toOut;
    unsigned afterDec = 0;
    while (true) {
        // Even in the trivial case, we must print 0.0
        for (unsigned i = 1; i < 11; ++i) {
            // TODO: check the edge behavior. I assume that if i*digit >= max, then inf >= fp
            if ((i * digit > fp) || (i == 10)) {
                unsigned ii = i - 1;
                fp -= (ii * digit);
                toOut.push_back('0' + ii);
                if (precRem > 0 && (precRem < precStart || ii > 0))
                    --precRem;
                break;
            }
        }
        digit /= 10.0;
        // should go from 1.0 -> 0.1, but use 0.5 as cut off since it is the middle and allows wiggle room
        if (digit <= 0.5 && ++afterDec > 1 && (fp == 0.0 || precRem == 0)) {
            // We are done! Round the remaining value, if any
            if (fp >= (digit * 5.0)) {
                unsigned at = 1;
                for (; at < toOut.size(); ++at) {
                    unsigned i = toOut.size() - at;
                    if (toOut[i] != '9') {
                        toOut[i]++;
                        break;
                    } else
                        toOut[i] = '0';
                }
                if (at == toOut.size()) {
                    out << '1';
                    --afterDec;
                }
            }

            break;
        }
    }

    // Truncate trailing zeros
    unsigned max = 1;
    for (; max < toOut.size() && max < afterDec - 1; ++max) {
        if (toOut[toOut.size() - max] != '0')
            break;
    }
    max -= 1;  // since when we break out, we see a nonzero
    max = toOut.size() - max;

    // Print out what we got (and include the decimal in its place!)
    for (unsigned i = 0; i < max; ++i) {
        out << toOut[i];
        if (toOut.size() - i == afterDec)
            out << '.';
    }

    if (scientific > 0)
        out << "E-" << scientific;
}
