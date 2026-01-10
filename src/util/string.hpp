/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_STRING_HPP
#define UTIL_STRING_HPP

#include <sstream>
#include <string>

/// @brief Get a repeated string.
/// @param num number of times to repeat a string.
/// @param str string to repeat.
/// @return single string of the repeated string.
std::string repeated_string(const unsigned num, const std::string& str);

/// @brief Force regular behavior for printing of floats.
/// Regular streaming is prone to truncation (note: not rounding), which confuses the interpreter's equivalence
/// algorithm.
/// @param out where to print the string representation of float, fp
/// @param fp the float to print. Must not be NaN or infinite!
void print_float(std::stringstream& out, double fp);
#endif
