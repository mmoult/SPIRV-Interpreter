/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_COMPARE_HPP
#define UTIL_COMPARE_HPP

#include <cmath>

namespace Compare {
bool eq_float(double x, double y, unsigned needed_sigfigs);

template<typename T>
T nbound(bool min, T a, T b) {
    // Edge cases:
    //   -0 is less than +0
    //   if one operand is NaN, return the other
    //   if both are NaN, return NaN
    if (std::isnan(a))
        return b;
    if (std::isnan(b))
        return a;
    bool a_neg = std::signbit(a);
    bool b_neg = std::signbit(b);
    const auto one = min ? a : b;
    const auto two = min ? b : a;
    if (a_neg != b_neg)
        return a_neg ? one : two;
    if (a < b)
        return one;
    return two;
}
};  // namespace Compare

#endif
