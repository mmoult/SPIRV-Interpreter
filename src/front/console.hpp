/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef FRONT_CONSOLE_HPP
#define FRONT_CONSOLE_HPP

#include <string>

inline bool suppress_warnings;

class Console {
    unsigned width;
    unsigned headerWidth;

public:
    inline explicit Console(unsigned header_width) {
        refresh(header_width);
    }

    void refresh(unsigned header_width);

    void print(std::string msg, const std::string& header = "");

    static void warn(std::string msg);
};
#endif
