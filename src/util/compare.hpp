/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_COMPARE_HPP
#define UTIL_COMPARE_HPP

#include <cassert>
#include <cmath>
#include <sstream>
#include <string>
#include <tuple>

#include "string.hpp"

inline std::string to_signed_string(float x) {
    std::stringstream f;
    if (!std::signbit(x))
        f << '+';
    // The opposite (-) will be added automatically
    print_float(f, x);
    return f.str();
}

enum class CompareMode {
    TYPICAL,  // expect each digit to match
    // If there is a discrepancy in typical, we need higher = 0 and the lower = 9 for all sigfigs
    // For example, 2.000004 and 1.999995
    X_HI,
    Y_HI,
    ZERO,  // If the sign differs. Eg: +0.000004 and -0.000004
};
/// Returns (hi, lo) based on the given comparison mode and inputs
inline std::tuple<char, char> select(CompareMode diff, char xc, char yc) {
    return (diff == CompareMode::X_HI) ? std::tuple(xc, yc) : std::tuple(yc, xc);
};

namespace Compare {
inline bool eq_float(float x, float y, unsigned needed_sigfigs) {
    if (x == y)
        return true;
    bool xnan = std::isnan(x);
    bool ynan = std::isnan(y);
    if (xnan || ynan)
        return xnan == ynan;  // nan != nan, but we'll allow it here
    bool xinf = std::isinf(x);
    bool yinf = std::isinf(y);
    if (xinf || yinf)
        return false;  // If either are inf, they must == each other

    // Compare sigfigs via string. Slow, but should do the job
    std::string xf = to_signed_string(x);
    unsigned xfl = xf.length();
    std::string yf = to_signed_string(y);
    unsigned yfl = yf.length();
    unsigned max_len = (xfl >= yfl) ? xfl : yfl;
    bool after_dec = false;

    CompareMode diff = (xf[0] == yf[0]) ? CompareMode::TYPICAL : CompareMode::ZERO;
    unsigned sigfigs = 0;
    for (unsigned i = 1; i < max_len; ++i) {
        char xc = (i >= xfl) ? (after_dec ? '0' : '.') : xf[i];
        char yc = (i >= yfl) ? (after_dec ? '0' : '.') : yf[i];

        // If we see dec for one, it must be for both (regardless of comparison mode)
        if (xc == '.' || yc == '.') {
            if (xc != yc)
                return false;
            after_dec = true;
            continue;
        }

        if (sigfigs >= needed_sigfigs) {
            if (diff == CompareMode::TYPICAL) {
                // both must round the same
                if ((xc >= '5') != (yc >= '5'))
                    return false;
            } else if (diff == CompareMode::ZERO) {
                if ((xc >= '5' || yc >= '5'))
                    return false;
            } else {
                // The low side must round up, high round down
                auto [hi, lo] = select(diff, xc, yc);
                if (lo < '5' || hi >= '5')
                    return false;
            }

            if (!after_dec) {
                // If we haven't seen a decimal, we must continue until we do (to verify that both
                // are in the same power of 10).
                for (++i; i < max_len; ++i) {
                    if (i >= xfl || i >= yfl)
                        return false;
                    char xc = xf[i];
                    char yc = yf[i];
                    if (xc == '.' || yc == '.') {
                        if (xc != yc)
                            return false;
                        break;
                    }
                }
            }
            return true;
        }

        if (diff == CompareMode::TYPICAL) {
            if (xc != yc) {
                // The characters are not the same, but we could still have a match.
                // Consider 2 and 1.999995. Rounding the latter to 6 sigfigs gets us
                // 2.00000, which is a match.

                // the first diff can only be off by 1
                char hi, lo;
                if (xc > yc) {
                    hi = xc;
                    lo = yc;
                    diff = CompareMode::X_HI;
                } else {  // yc > xc
                    hi = yc;
                    lo = xc;
                    diff = CompareMode::Y_HI;
                }
                if (hi - lo > 1)
                    return false;
            }  // if a match, do nothing
        } else if (diff == CompareMode::ZERO) {
            if (xc != yc || xc != '0')
                return false;
        } else {
            // hi must be 0 and lo must be 9
            auto [hi, lo] = select(diff, xc, yc);
            if (hi != '0' || lo != '9')
                return false;
        }

        assert(xc >= '0' && xc <= '9');
        ++sigfigs;
    }

    // If the compare mode was dichotomy (either X_HI or Y_HI), then encountering the end means the next character for
    // each is 0. This breaks the needed pattern.
    return diff == CompareMode::TYPICAL || diff == CompareMode::ZERO;
}
};  // namespace Compare
#endif
