/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#if defined(WIN32) || defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <stdio.h>
    #include <unistd.h>
#endif

#include <iostream>
#include <sstream>
#include <string>

export module front.console;

export class Console {
    unsigned width;
    unsigned headerWidth;

public:
    Console(unsigned header_width) {
        refresh(header_width);
    }

    void refresh(unsigned header_width) {
        headerWidth = header_width;
#if defined(WIN32) || defined(_WIN32)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        width = static_cast<unsigned>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
#else
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        width = w.ws_col;
#endif
    }

    void print(std::string msg, std::string header = "") {
        unsigned len = 0;
        bool use_header = !header.empty();
        bool crunched = headerWidth + 10 >= width;
        if (use_header) {
            std::cout << "  " << header;
            len += 2 + header.length();
            // If the header exceeded the header width (should be atypical), print on the next line. Otherwise, print
            // spaces until the header width ends
            if (len > headerWidth || len > width) {
                std::cout << '\n';
                len = 0;
                if (!crunched) {
                    std::cout << std::string(headerWidth, ' ');
                    len += headerWidth;
                }
            } else {
                std::cout << std::string(headerWidth - len, ' ');
                len = headerWidth;
            }
        }
        while (len + msg.length() > width) {
            // Find a convenient breaking point starting from as much as will fit on the line
            unsigned fit = width - len;
            unsigned breakAt = fit;
            for (; breakAt > 0; --breakAt) {
                if (msg[breakAt] == ' ')
                    break;
            }
            bool breakFound = breakAt != 0;
            if (!breakFound)
                breakAt = fit;

            std::cout << msg.substr(0, breakAt) << '\n';
            len = 0;
            if (use_header && !crunched) {
                std::cout << std::string(headerWidth, ' ');
                len += headerWidth;
            }
            msg = msg.substr(breakAt + (breakFound? 1:0));
        }
        std::cout << msg << std::endl;
        if (crunched)  // print an extra newline to separate entries
            std::cout << std::endl;
    }
};
