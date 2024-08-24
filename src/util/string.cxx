/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <string>
#include <sstream>
export module util.string;

/// @brief Get a repeated string.
/// @param num number of times to repeat a string.
/// @param str string to repeat.
/// @return single string of the repeated string.
export std::string repeatedString(const unsigned num, const std::string& str) {
    std::stringstream out;
    for (unsigned i = 0; i < num; ++i)
        out << str;
    return out.str();
}
