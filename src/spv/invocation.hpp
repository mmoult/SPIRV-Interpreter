/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_INVOCATION_HPP
#define SPV_INVOCATION_HPP

#include <vector>

class DataView;
class Frame;

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
};
#endif
