#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

import toml;
import value;

enum ReturnCodes : int {
    OK = 0,
    INFO = 1,
    BAD_ARGS = 2,
};

int main(int argc, char* argv[]) {
    std::string format, in, out;
    bool verbose = false;
    ValueMap inputs;
    std::vector<std::string> spvasms;

    bool args_only = false;
    for (int i = 0; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--") {
            args_only = true;
            continue;
        } else if (arg == "-f" || arg == "--format") {
            if (++i < argc) {
                format = arg;
            } else {
                std::cerr << "Missing argument for flag format!" << std::endl;
                return ReturnCodes::BAD_ARGS;
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "spirv-run - Interpret SPIR-V shaders" << std::endl;
            std::cout << std::endl;
            std::cout << "Usage: spirv-run [options] SPV+" << std::endl;
            std::cout << std::endl;
            std::cout << "where 'SPV' is a path to a spvasm file. One or more spvasm files may be " << std::endl;
            std::cout << "provided. If multiple input files have an OpEntry, the first appearing will be" << std::endl;
            std::cout << "used for the execution." << std::endl;
            std::cout << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -f / --format TOML  creates a template input file with stubs for all needed" << std::endl;
            std::cout << "                      inputs." << std::endl;
            std::cout << "  -h / --help         print this help and exit" << std::endl;
            std::cout << "  -i / --in TOML      specify a file to fetch input from. Alternatively, input" << std::endl;
            std::cout << "                      may be specified in key=value pairs with --set." << std::endl;
            std::cout << "  -o / --out TOML     specify a file to output to. Otherwise defaults to stdout" << std::endl;
            std::cout << "  -p / -print         enable vebose printing" << std::endl;
            std::cout << "  --set VAR=VAL       define input in the format of VAR=VAL pairs. May be given" << std::endl;
            std::cout << "                      more than once." << std::endl;
            std::cout << "  -v / --version      print version info and exit" << std::endl;
            return ReturnCodes::INFO;
        } else if (arg == "-i" || arg == "--in") {
            if (++i < argc) {
                in = arg;
            } else {
                std::cerr << "Missing argument for flag input!" << std::endl;
                return ReturnCodes::BAD_ARGS;
            }
        } else if (arg == "-o" || arg == "--out") {
            if (++i < argc) {
                out = arg;
            } else {
                std::cerr << "Missing argument for flag output!" << std::endl;
                return ReturnCodes::BAD_ARGS;
            }
        } else if (arg == "-p" || arg == "--print") {
            verbose = true;
        } else if (arg == "--set") {
            if (++i >= argc) {
                std::cerr << "Missing key=val pair argument for flag set!" << std::endl;
                return ReturnCodes::BAD_ARGS;
            }
            // find the delimiter, the first (and should be only) '=':
            int split = arg.find('=');
            if (split == -1) {
                std::cerr << "Missing delimiter (=) in key=val pair argument for flag set!" << std::endl;
                return ReturnCodes::BAD_ARGS;
            }
            // Parse the value and save in the key
            if (!Toml::parse_toml_value(inputs, arg.substr(0, split), arg.substr(split))) {
                // Expect toml parser to give us an error message.
                return ReturnCodes::BAD_ARGS;
            }
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "SPIRV-Interpreter version 0.0.1" << std::endl;
            return ReturnCodes::INFO;
        } else {
            spvasms.push_back(arg);
        }
    }

    if (spvasms.empty()) {
        std::cout << "Missing spvasm input!" << std::endl;
        return ReturnCodes::BAD_ARGS;
    }

    return ReturnCodes::OK;
}