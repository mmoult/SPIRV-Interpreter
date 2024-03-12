/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

import program;
import toml;
import value;

constexpr auto VERSION = "0.1.1";

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

class Line : public std::string {};
std::istream &operator>>(std::istream &is, Line &l) {
    std::getline(is, l);
    return is;
}

ReturnCode load_file(ValueMap& values, std::string& file_name) {
    // Parse the variables in file
    std::ifstream ifs(file_name);
    if (!ifs.is_open()) {
        std::cerr << "Could not open file \"" << file_name << "\"!" << std::endl;
        return ReturnCode::BAD_FILE;
    }
    if (!Toml::parse_toml(values, std::istream_iterator<Line>(ifs), std::istream_iterator<Line>())) {
        // Expect toml parser to give us an error message.
        return ReturnCode::BAD_PARSE;
    }
    return ReturnCode::OK;
}

int main(int argc, char* argv[]) {
    std::string itemplate, in, out, check;
    bool verbose = false;
    ValueMap inputs;
    std::optional<std::string> spv;

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
                std::cout << "spirv-run - Interpret SPIR-V shaders" << std::endl;
                std::cout << std::endl;
                std::cout << "Usage: spirv-run [options] SPV" << std::endl;
                std::cout << std::endl;
                std::cout << "where 'SPV' is a path to a spv file, which must have an OpEntry instruction." << std::endl;
                std::cout << std::endl;
                std::cout << "Options:" << std::endl;
                std::cout << "  -c / --check TOML     checks the output against the specified file, returning" << std::endl;
                std::cout << "                        0 if equal." << std::endl;
                std::cout << "  -h / --help           print this help and exit" << std::endl;
                std::cout << "  -i / --in TOML        specify a file to fetch input from. Alternatively, input" << std::endl;
                std::cout << "                        may be specified in key=value pairs with --set." << std::endl;
                std::cout << "  -o / --out TOML       specify a file to output to. Defaults to stdout" << std::endl;
                //std::cout << "  -p / -print           enable vebose printing" << std::endl;
                std::cout << "  --set VAR=VAL         define input in the format of VAR=VAL pairs. May be" << std::endl;
                std::cout << "                        given more than once." << std::endl;
                std::cout << "  -t / --template TOML  creates a template input file with stubs for all needed" << std::endl;
                std::cout << "                        inputs." << std::endl;
                std::cout << "  -v / --version        print version info and exit" << std::endl;
                return ReturnCode::INFO;
            }

#define NEXT(SAVE) \
    if (++i < argc) \
        SAVE = std::string(argv[i]); \
    else { \
        std::cerr << "Missing argument for flag " << arg << "!" << std::endl; \
        return ReturnCode::BAD_ARGS; \
    } \
            
            else if (arg == "-c" || arg == "--check") {
                NEXT(check);
            } else if (arg == "-i" || arg == "--in") {
                NEXT(in);
            } else if (arg == "-o" || arg == "--out") {
                NEXT(out);
            } else if (arg == "-p" || arg == "--print") {
                verbose = true;
            } else if (arg == "--set") {
                if (++i >= argc) {
                    std::cerr << "Missing key=val pair argument for flag set!" << std::endl;
                    return ReturnCode::BAD_ARGS;
                }
                // find the delimiter, the first (and should be only) '=':
                std::string set(argv[i]);
                int split = set.find('=');
                if (split == -1) {
                    std::cerr << "Missing delimiter (=) in key=val pair argument for flag set!" << std::endl;
                    return ReturnCode::BAD_ARGS;
                }
                // Parse the value and save in the key
                if (!Toml::parse_toml_value(inputs, set.substr(0, split), set.substr(split))) {
                    // Expect toml parser to give us an error message.
                    return ReturnCode::BAD_ARGS;
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

    if (!in.empty()) {
        auto res = load_file(inputs, in);
        if (res != ReturnCode::OK)
            return res;
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
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PARSE;
    }
    delete[] buffer; // delete source now that it has been replaced with program

    if (!itemplate.empty()) {
        // Print out needed variables to file specified
        std::stringstream ss;
        program.printInputs(ss);
        std::ofstream templateFile(itemplate);
        templateFile << ss.str();
        templateFile.close();
        return ReturnCode::INFO;
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

    // Output the result
    std::stringstream ss;
    program.printOutputs(ss);
    if (out.empty() && check.empty()) {
        // Default to stdout, but don't print if checking is enabled
        std::cout << ss.str() << std::flush;
    } else {
        std::ofstream outFile(out);
        outFile << ss.str();
        outFile.close();
    }

    if (!check.empty()) {
        ValueMap check_map;
        load_file(check_map, check);
        if (!program.checkOutputs(check_map)) {
            std::cerr << "Output did NOT match!" << std::endl;
            return ReturnCode::BAD_COMPARE;
        } else
            std::cout << "Outputs match!" << std::endl;

        for (const auto& [_, val] : check_map)
            delete val;
    }

    // Clean up before successful exit
    for (const auto& [_, val] : inputs)
        delete val;

    return ReturnCode::OK;
}
