module;
#include <vector>

import utils;
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

    Utils::May<Value*> getArg() {
        if (argCount >= args.size())
            return Utils::unexpected<Value*>("No more args to use!");
        Value* ret = args[argCount];
        ++argCount;
        ++pc;
        return Utils::expected<Value*>(ret);
    }

    Utils::May<bool> incPC() {
        if (argCount < args.size())
            return Utils::unexpected<bool>("Unused function argument(s)!");
        ++pc;
        return Utils::expected();
    }

    Utils::May<bool> setPC(unsigned pc) {
        if (argCount < args.size())
            return Utils::unexpected<bool>("Unused function argument(s)!");
        this->pc = pc;
        return Utils::expected();
    }

    unsigned getReturn() {
        return retAt;
    }
    bool hasReturn() {
        return retAt != 0;
    }
};
