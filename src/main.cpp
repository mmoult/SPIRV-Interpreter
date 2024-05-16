/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#if defined(WIN32) || defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <stdio.h>
    #include <unistd.h>
#endif

#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "values/value.hpp"

import format.json;
import format.parse;
import format.yaml;
import program;

constexpr auto VERSION = "0.3.0";

enum ReturnCode : int {
    OK = 0,
    INFO = 1,
    BAD_ARGS = 2,
    BAD_FILE = 3,
    BAD_PARSE = 4,
    BAD_PROG_INPUT = 5,
    FAILED_EXE = 6,
    BAD_COMPARE = 7,
};

Yaml yaml;
Json json;
const unsigned NUM_FORMATS = 2;
std::string format_names[] = {"yaml", "json"};
ValueFormat* format_vals[] = {&yaml, &json};

ValueFormat* determine_format(const std::string& file_name, ValueFormat* preference, bool exact = false) {
    std::string to_match = file_name;
    if (!exact) {
        if (size_t dot = file_name.rfind('.'); dot != std::string::npos)
            to_match = file_name.substr(dot + 1);
    }

    for (unsigned i = 0; i < NUM_FORMATS; ++i) {
        if(to_match == format_names[i])
            return format_vals[i];
    }
    return preference;
}

ReturnCode load_file(ValueMap& values, std::string& file_name, ValueFormat* preference) {
    // Parse the variables in file
    std::ifstream ifs(file_name);
    if (!ifs.is_open()) {
        std::cerr << "Could not open file \"" << file_name << "\"!" << std::endl;
        return ReturnCode::BAD_FILE;
    }
    ValueFormat* format = determine_format(file_name, preference);
    try {
        format->parseFile(values, ifs);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PARSE;
    }
    return ReturnCode::OK;
}

void print(std::string msg, unsigned width, std::string header = "", unsigned header_width = 0) {
    unsigned len = 0;
    bool use_header = !header.empty();
    bool crunched = header_width + 10 >= width;
    if (use_header) {
        std::cout << "  " << header;
        len += 2 + header.length();
        // If the header exceeded the header width (should be atypical), print on the next line. Otherwise, print
        // spaces until the header width ends
        if (len > header_width || len > width) {
            std::cout << '\n';
            len = 0;
            if (!crunched) {
                std::cout << std::string(header_width, ' ');
                len += header_width;
            }
        } else {
            std::cout << std::string(header_width - len, ' ');
            len = header_width;
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
            std::cout << std::string(header_width, ' ');
            len += header_width;
        }
        msg = msg.substr(breakAt + (breakFound? 1:0));
    }
    std::cout << msg << std::endl;
    if (crunched)  // print an extra newline to separate entries
        std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    std::string itemplate, in, out, check;
    ValueFormat* format = &yaml;
    bool verbose = false;
    ValueMap inputs;
    std::optional<std::string> spv;
    unsigned indent_size = 0;
    bool never_templatize = false;

    bool args_only = false;
    // Remember to skip argv[0] which is the path to the executable
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--") {
            args_only = true;
            continue;
        }

        if (!args_only) { // check for flags
            bool found = true;

            // Help first, then alphabetic
            if (arg == "-h" || arg == "--help") {
                unsigned width;
#if defined(WIN32) || defined(_WIN32)
                CONSOLE_SCREEN_BUFFER_INFO csbi;
                GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
                width = static_cast<unsigned>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
#else
                struct winsize w;
                ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
                width = w.ws_col;
#endif

                unsigned hwidth = 24;
                print("spirv-run - Interpret SPIR-V shaders", width);
                print("", width);
                print("Usage: spirv-run [options] SPV", width);
                print("where 'SPV' is a path to a spv file, which must have an OpEntry instruction.", width);
                print("", width);
                print("Options:", width);
                print(
                    "Checks the output against the specified file, returning 0 if equal.", width,
                    "-c / --check FILE", hwidth
                );
                //print("Launch an interactive execution. Enables -p implicitly.", width, "-d / --debug", hwidth);
                print("Print default values in any template file generated", width, "--default", hwidth);
                print(
                    "Specify a default value format {\"yaml\", \"json\"}. The interpreter will try to assume desired "
                    "format from the extension of the file to read/write, but this argument is still useful for --set "
                    "pairs, stdout, or if the extension is not recognized. Defaults to \"yaml\".", width,
                    "-f / --format", hwidth
                );
                print("Print this help and exit.", width, "-h / --help", hwidth);
                print(
                    "Specify a file to fetch input from. Alternatively, input may be specified in key=value pairs with "
                    "--set.", width,
                    "-i / --in FILE", hwidth
                );
                print("Specify the size of each indent (in spaces) for outputs.", width, "--indent SIZE", hwidth);
                print("Specify a file to output to. Defaults to stdout.", width, "-o / --out FILE", hwidth);
                print("Enable vebose printing.", width, "-p / --print", hwidth);
                print(
                    "Define key-value pair in the default format. May be given more than once.", width,
                    "--set KEY_VAL", hwidth
                );
                print(
                    "Creates a template input file with stubs for all needed inputs. If --default is set, the default "
                    "values will be printed instead of <type> stubs.", width,
                    "-t / --template FILE", hwidth
                );
                print("Print version info and exit.", width, "-v / --version", hwidth);
                return ReturnCode::INFO;
            }

#define NEXT(SAVE) \
    if (++i < argc) \
        SAVE = std::string(argv[i]); \
    else { \
        std::cerr << "Missing argument for flag " << arg << "!" << std::endl; \
        return ReturnCode::BAD_ARGS; \
    }

            else if (arg == "-c" || arg == "--check") {
                NEXT(check);
            } else if (arg == "--default") {
                never_templatize = true;
            } else if (arg == "-f" || arg == "--format") {
                std::string s_format;
                NEXT(s_format);
                format = determine_format(s_format, nullptr, true);
                if (format == nullptr) {
                    std::cerr << "Unrecognized file format: " << s_format << std::endl;
                    return ReturnCode::BAD_ARGS;
                }
            } else if (arg == "-i" || arg == "--in") {
                NEXT(in);
            } else if (arg == "--indent") {
                std::string indent_size_str;
                NEXT(indent_size_str);
                try {
                    int parsed = std::stoi(indent_size_str, nullptr);
                    if (parsed <= 0) {
                        std::cerr << "The number of spaces per indent must be > 0, but " << parsed << " was found!";
                        return ReturnCode::BAD_ARGS;
                    }
                    indent_size = static_cast<unsigned>(parsed);
                } catch (const std::exception& ex) {
                    std::cerr << "Could not parse argument for --indent! The number of spaces per indent must be an "
                                 "integer. Found string: \"";
                    std::cerr << indent_size_str << "\"";
                    return ReturnCode::BAD_ARGS;
                }
            } else if (arg == "-o" || arg == "--out") {
                NEXT(out);
            } else if (arg == "-p" || arg == "--print") {
                verbose = true;
            } else if (arg == "--set") {
                if (++i >= argc) {
                    std::cerr << "Missing key-val pair argument for flag set!" << std::endl;
                    return ReturnCode::BAD_ARGS;
                }

                // Parse the value and save in the key
                std::string set = argv[i];
                try {
                    format->parseVariable(inputs, set);
                } catch (const std::exception& e) {
                    std::cerr << e.what() << std::endl;
                    return ReturnCode::BAD_PARSE;
                }
            } else if (arg == "-t" || arg == "--template") {
                NEXT(itemplate);
            } else if (arg == "-v" || arg == "--version") {
                std::cout << "SPIRV-Interpreter version " << VERSION << std::endl;
                return ReturnCode::INFO;
            }
#undef NEXT
            else
                found = false;

            if (found)
                continue;
        }

        // If we are here, there were no matches on flags, so this must be a positional arg
        if (spv.has_value()) {
            // There may only be one
            std::cerr << "Multiple spv inputs given! Second input is " << arg << "." << std::endl;
            return ReturnCode::BAD_ARGS;
        } else {
            spv = std::optional(arg);
        }
    }

    if (!spv.has_value()) {
        std::cerr << "Missing spv input!" << std::endl;
        return ReturnCode::BAD_ARGS;
    }

    // Load the SPIR-V input file:
    std::ifstream ifs(spv.value(), std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "Could not open source file \"" << spv.value() << "\"!" << std::endl;
        return ReturnCode::BAD_FILE;
    }
    // get its size:
    ifs.seekg(0, ifs.end);
    int length = ifs.tellg();
    ifs.seekg(0, ifs.beg);
    // allocate memory:
    char* buffer = new char[length];
    // read data as a block:
    ifs.read(buffer, length);
    ifs.close();

    // The signedness of char is implementation defined. Use uint8_t to remove ambiguity
    Spv::Program program;
    try {
        program.parse(std::bit_cast<uint8_t*>(buffer), length);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PARSE;
    }
    delete[] buffer; // delete source now that it has been replaced with program

    if (!itemplate.empty()) {
        // Print out needed variables to file specified
        std::stringstream ss;
        const auto& prog_ins = program.getInputs();
        ValueFormat* format2 = determine_format(itemplate, format);
        if (indent_size > 0)
            format2->setIndentSize(indent_size);
        if (!never_templatize)
            format2->setTemplate(true);
        format2->printFile(ss, prog_ins);
        format2->setTemplate(false);

        std::ofstream templateFile(itemplate);
        templateFile << ss.str();
        templateFile.close();
        return ReturnCode::INFO;
    }

    if (!in.empty()) {
        auto res = load_file(inputs, in, format);
        if (res != ReturnCode::OK)
            return res;
    }

    // Verify that the inputs loaded match what the program expects
    try {
        program.setup(inputs);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PROG_INPUT;
    }

    // Run the program
    try {
        program.execute(verbose);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::FAILED_EXE;
    }

    // Output the result (If file output is not set, only print to stdout if checking isn't enabled)
    if (bool out_set = !out.empty(); check.empty() || out_set) {
        std::stringstream ss;
        const auto& prog_outs = program.getOutputs();

        if (out_set) {
            ValueFormat* format2 = determine_format(out, format);
            if (indent_size > 0)
                format2->setIndentSize(indent_size);
            format2->printFile(ss, prog_outs);

            std::ofstream outFile(out);
            outFile << ss.str();
            outFile.close();
        } else {
            format->printFile(ss, prog_outs);
            std::cout << ss.str() << std::flush;
        }
    }

    if (!check.empty()) {
        ValueMap check_map;
        load_file(check_map, check, format);
        auto [ok, total_tests] = program.checkOutputs(check_map);
        if (!ok) {
            std::cerr << "Output did NOT match!" << std::endl;
            return ReturnCode::BAD_COMPARE;
        } else
            // Print the number of variables checked to know whether it was a trivial pass
            std::cout << total_tests;
            if (total_tests == 1)
                std::cout << " output matches!";
            else
                std::cout << " outputs match!";
            std::cout << std::endl;

        for (const auto& [_, val] : check_map)
            delete val;
    }

    // Clean up before successful exit
    for (const auto& [_, val] : inputs)
        delete val;

    return ReturnCode::OK;
}
