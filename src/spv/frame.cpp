/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "frame.hpp"


static void delete_owned_result(Value*& result, RtStageKind trigger) {
    if (result != nullptr && (trigger == RtStageKind::INTERSECTION || trigger == RtStageKind::ANY_HIT))
        delete result;
    result = nullptr;
}

static void delete_owned_hit_attribute(Value*& hit_attribute, RtStageKind trigger) {
    if (hit_attribute != nullptr && (trigger == RtStageKind::INTERSECTION || trigger == RtStageKind::CLOSEST))
        delete hit_attribute;
    hit_attribute = nullptr;
}

Frame::~Frame() {
    delete_owned_result(this->rt.result, this->rt.trigger);
    delete_owned_hit_attribute(this->rt.hitAttribute, this->rt.trigger);
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

void Frame::triggerRaytrace(
    RtStageKind stage,
    unsigned index,
    Value* result,
    Value* hit_attrib,
    AccelStruct& as,
    Value* payload
) {
    this->rt.trigger = stage;
    this->rt.index = index;
    this->rt.as = &as;
    this->rt.result = result;
    this->rt.payload = payload;
    this->rt.hitAttribute = hit_attrib;
    this->rt.callableReturning = false;
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
    this->rt.payload = nullptr;

    // Callable substages have no hit attribute.
    this->rt.hitAttribute = nullptr;
    this->rt.callableReturning = false;

    if (this->rt.data != nullptr) {
        delete this->rt.data;
        this->rt.data = nullptr;
    }
}

void Frame::disableRaytrace() {
    delete_owned_result(this->rt.result, this->rt.trigger);
    delete_owned_hit_attribute(this->rt.hitAttribute, this->rt.trigger);
    this->rt.trigger = RtStageKind::NONE;
    this->rt.callableReturning = false;
    this->rt.index = 0;
    this->rt.as = nullptr;
    this->rt.payload = nullptr;
    delete this->rt.data;
    this->rt.data = nullptr;
}
