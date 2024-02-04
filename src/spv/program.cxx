module;
#include <string>

import utils;
export module program;

export namespace Spv {
    class Program {
        Program() = default; // define here to make private

    public:
        static Utils::May<Program> parse(char* buffer, int length) {
            Program* program = new Program();

            return Utils::May<Program>::some(*program);
        }
    };
};
