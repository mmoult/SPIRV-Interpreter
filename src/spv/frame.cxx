/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <stdexcept>
#include <vector>

import value;
export module frame;

export class Frame {
    unsigned pc;
    std::vector<Value*> args;
    unsigned argCount;
    /// Where to store the return value, if any. Should be 0 if no return expected
    unsigned retAt;

public:
    Frame(unsigned pc, std::vector<Value*>& args, unsigned ret_at): pc(pc), args(args), argCount(0), retAt(ret_at) {}

    unsigned getPC() {
        return pc;
    }

    Value* getArg() noexcept(false) {
        if (argCount >= args.size())
            throw std::runtime_error("No more args to use!");
        Value* ret = args[argCount];
        ++argCount;
        ++pc;
        return ret;
    }

    void incPC() noexcept(false) {
        if (argCount < args.size())
            throw std::runtime_error("Unused function argument(s)!");
        ++pc;
    }

    void setPC(unsigned pc) noexcept(false) {
        if (argCount < args.size())
            throw std::runtime_error("Unused function argument(s)!");
        this->pc = pc;
    }

    unsigned getReturn() {
        return retAt;
    }
    bool hasReturn() {
        return retAt != 0;
    }
};
