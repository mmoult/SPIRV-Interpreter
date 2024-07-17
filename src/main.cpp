/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include "values/value.hpp"
import format.json;
import format.parse;
import format.yaml;
import front.argparse;
import front.console;
import spv.program;

constexpr auto VERSION = "0.6.0";

enum ReturnCode : int {
    OK = 0,
    INFO = 1,
    BAD_ARGS = 2,
    BAD_FILE = 3,
    BAD_PARSE = 4,
    BAD_PROGRAM = 5,
    BAD_PROG_INPUT = 6,
    FAILED_EXE = 7,
    BAD_COMPARE = 8,
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

ReturnCode load_file(ValueMap& values, const std::string& file_name, ValueFormat* preference) {
    try {
        if (file_name == "-") {
            // TODO message to indicate the program expects user input and how to terminate
            preference->parseFile(values, std::cin);
        } else {
            // Parse the variables in file
            std::ifstream ifs(file_name);
            if (!ifs.is_open()) {
                std::cerr << "Could not open file \"" << file_name << "\"!" << std::endl;
                return ReturnCode::BAD_FILE;
            }
            ValueFormat* format = determine_format(file_name, preference);
            format->parseFile(values, ifs);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PARSE;
    }

    return ReturnCode::OK;
}

int main(int argc, char* argv[]) {
    ArgParse::Parser parser;

    ArgParse::StringOption check("FILE");
    parser.addOption(&check, "check", "Check the output against the specified file. Returns 0 if equal.", "c");
    ArgParse::Flag debug;
    parser.addOption(&debug, "debug", "Launch an interactive execution. Enables --print implicitly.", "d");
    ArgParse::Flag generate;
    parser.addOption(
        &generate,
        "default",
        "Generate default values for the template file instead of <type> stubs. Enables --template implicitly.",
        "g"
    );
    ArgParse::StringOption format_arg("FORMAT", "yaml");
    parser.addOption(
        &format_arg,
        "format",
        "Specify a default value format from {\"yaml\", \"json\"}. The interpreter will try to assume desired format "
        "from the extension of the file to read/write, but this argument is still useful for --set pairs, stdout, or "
        "if the extension is not recognized. Defaults to \"yaml\".",
        "f"
    );
    ArgParse::Flag help;
    parser.addOption(&help, "help", "Print this help and exit.", "h");
    ArgParse::StringOption in_arg("FILE");
    parser.addOption(
        &in_arg,
        "in",
        "Specify a file to fetch input from. Alternatively, input may be specified in key-value pairs with --set.",
        "i"
    );
    ArgParse::UintOption indent_arg("SIZE", 2);
    parser.addOption(
        &indent_arg,
        "indent",
        "Specify the size of each indent (in spaces) for outputs. Defaults to 2",
        "n"
    );
    ArgParse::StringOption out_arg("FILE", "-");
    parser.addOption(
        &out_arg,
        "out",
        "Specify a file to output to. By default, output prints to stdout.",
        "o"
    );
    ArgParse::Flag verbose;
    parser.addOption(&verbose, "print", "Enable verbose printing.", "p");
    ArgParse::StringOption set_arg("KEY_VAL");
    parser.addOption(&set_arg, "set", "Define key-value pair in the default format. May be given more than once.", "s");
    ArgParse::StringOption template_arg("FILE");
    parser.addOption(
        &template_arg,
        "template",
        "Creates a template input file with stubs for all needed inputs. If --default is set, the default values will "
        "be printed instead of <type> stubs.",
        "t"
    );
    ArgParse::Flag version;
    parser.addOption(&version, "version", "Print version info and exit.", "v");
    ArgParse::StringOption spv_arg("FILE");
    parser.addPositional(
        &spv_arg,
        "spv input",
        false
    );

    if (!parser.parse(argc, argv))
        return ReturnCode::BAD_ARGS;

    // Perform actions which don't require positional arguments (such as help and version):
    if (help.enabled) {
        std::vector<std::string> help_intro{
            "spirv-run - Interpret SPIR-V shaders",
            "",
            "Usage: spirv-run [options] SPV",
            "where 'SPV' is a path to a spv file, which must have an OpEntry instruction.",
            "",
            "Options may be given in any order or not at all. For all options which accept FILE as an argument,"
            " \"-\" may be given to use stdin or stdout instead. The list of options is given below:",
        };
        parser.printHelp(24, help_intro);
        return ReturnCode::INFO;
    }
        if (version.enabled) {
        std::cout << "SPIRV-Interpreter version " << VERSION << std::endl;
        std::cout << "https://github.com/mmoult/SPIRV-Interpreter" << std::endl;
#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)
        std::cout << "Commit hash: " << STRINGIZE_VALUE_OF(HASH) << std::endl;
#undef STRINGIZE_VALUE_OF
#undef STRINGIZE
        return ReturnCode::INFO;
    }

    // Verify that our positional argument was filled
    if (!spv_arg.isPresent()) {
        std::cerr << "Missing positional argument: spv input" << std::endl;
        return ReturnCode::BAD_ARGS;
    }

    // Peform the rest of the option actions
    if (debug.enabled)
        verbose.enabled = true;
    if (generate.enabled && !template_arg.hasValue())
        template_arg.setValue("-");

    ValueFormat* format = determine_format(format_arg.getValue(), nullptr, true);
    assert(format != nullptr);

    // Load the SPIR-V input file:
    std::ifstream ifs(spv_arg.getValue(), std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "Could not open source file \"" << spv_arg.getValue() << "\"!" << std::endl;
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
    Program program;
    try {
        program.parse(std::bit_cast<uint8_t*>(buffer), length);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PARSE;
    }
    delete[] buffer; // delete source now that it has been replaced with program

    // We must load the inputs, if any, before init. This is because specialization constants must know their input.
    // Indeed, if the size of input variables is dependent on specialization constants (which it might be), then it is
    // vital that the correct input is used before we generate the template.
    // Of course, even if there are specialization constants, they should have some default value which will be used if
    // the user doesn't provide something to override it.
    ValueMap inputs;
    if (in_arg.isPresent()) {
        auto res = load_file(inputs, in_arg.getValue(), format);
        if (res != ReturnCode::OK)
            return res;
    }
    if (set_arg.isPresent()) {
        // Parse the value and save in the key
        try {
            for (std::string val : set_arg.getValues())
                format->parseVariable(inputs, val);
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return ReturnCode::BAD_PARSE;
        }
    }

    try {
        program.init(inputs);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PROGRAM;
    }

    if (template_arg.isPresent()) {
        std::string itemplate = template_arg.getValue();
        // Print out needed variables to file specified
        std::stringstream ss;
        auto prog_ins = program.getInputs();
        ValueFormat* format2 = determine_format(itemplate, format);
        if (indent_arg.isPresent())
            format2->setIndentSize(indent_arg.getValue());
        format2->setTemplate(!generate.enabled);
        format2->printFile(ss, prog_ins);

        if (itemplate == "-") {
            std::cout << ss.str() << std::endl;
        } else {
            std::ofstream templateFile(itemplate);
            templateFile << ss.str();
            templateFile.close();
        }
        return ReturnCode::INFO;
    }

    // Verify that the inputs loaded match what the program expects
    try {
        program.checkInputs(inputs);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::BAD_PROG_INPUT;
    }

    // Run the program
    try {
        program.execute(verbose.enabled, debug.enabled, *format);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return ReturnCode::FAILED_EXE;
    }

    // Output the result (If file output is not set, only print to stdout if checking isn't enabled)
    if (bool out_set = out_arg.isPresent(); !check.isPresent() || out_set) {
        std::stringstream ss;
        const auto& prog_outs = program.getOutputs();

        std::string out = out_arg.getValue();
        if (out_set && out != "-") {
            ValueFormat* format2 = determine_format(out, format);
            if (indent_arg.isPresent())
                format2->setIndentSize(indent_arg.getValue());
            format2->printFile(ss, prog_outs);

            std::ofstream outFile(out);
            outFile << ss.str();
            outFile.close();
        } else {
            format->printFile(ss, prog_outs);

            if (verbose.enabled)
                std::cout << "\nResults=" << std::endl;
            // The file print should end with a newline, so omit that here but still flush
            std::cout << ss.str() << std::flush;
        }
    }

    if (check.isPresent()) {
        ValueMap check_map;
        load_file(check_map, check.getValue(), format);
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
