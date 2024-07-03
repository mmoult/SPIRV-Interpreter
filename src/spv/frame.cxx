/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <stdexcept>
#include <vector>

#include "../values/value.hpp"
#include "data/manager.h"
export module spv.frame;

export class Frame {
    unsigned pc;

    // pair of label values used for phis. You can only read one and only set the other
    unsigned curLabel;
    unsigned lastLabel;

    // Function calls put their arguments on the frame, then the function must pull all arguments, one per instruction,
    // before any other instruction is seen:
    // Function
    // OpFunctionParameter
    // ...
    // OpLabel
    std::vector<const Value*> args;
    /// Where to store the return value, if any. Should be 0 if no return expected
    unsigned retAt;

    /// @brief The view of data for this frame.
    DataView* view;

    /// The argument index to use next
    unsigned argCount;
    /// Whether this instruction needs to use an argument before incrementing the PC
    bool first;

public:
    Frame(unsigned pc, std::vector<const Value*>& args, unsigned ret_at, DataManager& data) :
        pc(pc),
        curLabel(0),
        lastLabel(0),
        args(args),
        retAt(ret_at),
        view(data.makeView()),
        argCount(0),
        first(true) {}
    Frame(const Frame&) = delete;
    Frame& operator= (const Frame&) = delete;
    ~Frame() {
        while (argCount < args.size())
            delete getArg();
        view->getSource()->destroyView(view);
        view = nullptr;  // to prevent double deletion or after-deletion use
    }

    unsigned getPC() const {
        return pc;
    }

    const Value* getArg() noexcept(false) {
        if (argCount >= args.size())
            throw std::runtime_error("No more args to use!");
        ++pc;
        return args[argCount++];
    }

    void incPC() noexcept(false) {
        if (first)
            first = false;
        else if (argCount < args.size())
            throw std::runtime_error("Unused function argument(s)!");
        ++pc;
    }

    void setPC(unsigned pc) noexcept(false) {
        if (argCount < args.size())
            throw std::runtime_error("Unused function argument(s)!");
        this->pc = pc;
    }

    unsigned getReturn() const {
        return retAt;
    }
    bool hasReturn() const {
        return retAt != 0;
    }

    void setLabel(unsigned label) {
        lastLabel = curLabel;
        curLabel = label;
    }
    unsigned getLabel() const {
        return lastLabel;
    }

    DataView& getData() {
        return *view;
    }
};
