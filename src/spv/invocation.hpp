/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_INVOCATION_HPP
#define SPV_INVOCATION_HPP

#include <vector>

#include "data/manager.hpp"
#include "frame.hpp"

struct Invocation {
    enum class Status {
        READY,
        SLEEP,
        WAKE,
        FINISH,
    };
    std::vector<Frame*> frames;
    DataView* statics = nullptr;
    Status status = Status::READY;
    bool demoted = false;

    Invocation() = default;
    // The invocation owns its frames and view, so it must not be copied.
    Invocation(const Invocation&) = delete;
    Invocation& operator=(const Invocation&) = delete;

    // Release what is held, however execution was left (an error, or quitting the debugger, for example). The frames
    // are released first since they use the view.
    ~Invocation() {
        while (!frames.empty()) {
            delete frames.back();
            frames.pop_back();
        }
        if (statics != nullptr) {
            statics->getSource()->destroyView(statics);
            statics = nullptr;
        }
    }
};
#endif
