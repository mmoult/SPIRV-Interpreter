/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "frame.hpp"

#include "../values/primitive.hpp"

Frame::~Frame() {
    if (view != nullptr) {
        view->getSource()->destroyView(view);
        view = nullptr;  // to prevent double deletion or after-deletion use
    }
    if (this->rt.data != nullptr) {
        delete this->rt.data;
        this->rt.data = nullptr;
    }
}

Data& Frame::getArg() noexcept(false) {
    if (argCount >= args.size())
        throw std::runtime_error("No more args to use!");
    ++pc;
    return *args[argCount++];
}

void Frame::incPC() noexcept(false) {
    if (first)
        first = false;
    else if (argCount < args.size())
        throw std::runtime_error("Unused function argument(s)!");
    ++pc;
}

void Frame::triggerRaytrace(RtStageKind stage, unsigned index, Value* payload, Value* hit_attrib, AccelStruct& as) {
    this->rt.trigger = stage;
    this->rt.index = index;
    this->rt.as = &as;
    this->rt.result = payload;
    this->rt.hitAttribute = hit_attrib;
    if (this->rt.data != nullptr) {
        delete this->rt.data;
        this->rt.data = nullptr;
    }
}

void Frame::triggerCallable(unsigned index, Value* callable, AccelStruct* as) {
    this->rt.trigger = RtStageKind::CALLABLE;
    this->rt.index = index;
    this->rt.as = as;
    this->rt.result = callable;

    // hit attribute is never used by callable, so we reuse it to track whether this frame is entry or exit
    Primitive dummy(0);
    this->rt.hitAttribute = static_cast<Value*>(&dummy);

    if (this->rt.data != nullptr) {
        delete this->rt.data;
        this->rt.data = nullptr;
    }
}

void Frame::disableRaytrace() {
    this->rt.trigger = RtStageKind::NONE;
    this->rt.index = 0;
    this->rt.as = nullptr;
    this->rt.result = nullptr;
    delete this->rt.data;
    this->rt.data = nullptr;
}
