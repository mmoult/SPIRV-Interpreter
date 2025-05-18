/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <stdexcept>

#include "../values/value.hpp"
#include "data/manager.hpp"
export module spv.frame;
import value.raytrace.accelStruct;
import value.primitive;

export enum RtStageKind { NONE, ANY_HIT, CLOSEST, INTERSECTION, MISS, CALLABLE };
export const char* to_string(RtStageKind kind) {
    switch (kind) {
    case RtStageKind::NONE:
        return "none";
    case RtStageKind::ANY_HIT:
        return "any_hit";
    case RtStageKind::CLOSEST:
        return "closest_hit";
    case RtStageKind::INTERSECTION:
        return "intersection";
    case RtStageKind::MISS:
        return "miss";
    case RtStageKind::CALLABLE:
        return "callable";
    default:
        return "invalid";
    }
}

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
    std::vector<Data*> args;
    /// Where to store the return value, if any. Should be 0 if no return expected
    unsigned retAt;

    /// @brief The view of data for this frame.
    DataView* view;
    AccelStruct* fromAs;

    /// The argument index to use next
    unsigned argCount;
    /// Whether this instruction needs to use an argument before incrementing the PC
    bool first;

    struct {
        RtStageKind trigger = RtStageKind::NONE;
        unsigned index = 0;
        AccelStruct* as = nullptr;
        // Used as:
        // - the payload (for closest hit, miss)
        // - bool hit data (for intersection)
        // - [intersection_valid: bool, continue_search: bool] (for any hit)
        // - callable_data for callable substages
        Value* result = nullptr;
        Value* hitAttribute = nullptr;
        // the data which is a duplicate of the substage's
        DataView* data = nullptr;
    } rt;

public:
    Frame(unsigned pc, std::vector<Data*>& args, unsigned ret_at, DataView& data, AccelStruct* from_as = nullptr)
        : pc(pc)
        , curLabel(0)
        , lastLabel(0)
        , args(args)
        , retAt(ret_at)
        , view(data.getSource()->makeView(&data))
        , fromAs(from_as)
        , argCount(0)
        , first(true) {}
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    ~Frame() {
        if (view != nullptr) {
            view->getSource()->destroyView(view);
            view = nullptr;  // to prevent double deletion or after-deletion use
        }
        if (this->rt.data != nullptr) {
            delete this->rt.data;
            this->rt.data = nullptr;
        }
    }

    unsigned getPC() const {
        return pc;
    }

    Data& getArg() noexcept(false) {
        if (argCount >= args.size())
            throw std::runtime_error("No more args to use!");
        ++pc;
        return *args[argCount++];
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
    /// @brief removes the data view from this frame
    /// Necessary to preserve the data view, since deleting this frame deletes the data by default
    void removeData() {
        view = nullptr;
    }

    RtStageKind getRtTrigger() const {
        return rt.trigger;
    }

    void triggerRaytrace(RtStageKind stage, unsigned index, Value* payload, Value* hit_attrib, AccelStruct& as) {
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
    void triggerCallable(unsigned index, Value* callable, AccelStruct* as) {
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

    void disableRaytrace() {
        this->rt.trigger = RtStageKind::NONE;
        this->rt.index = 0;
        this->rt.as = nullptr;
        this->rt.result = nullptr;
        delete this->rt.data;
        this->rt.data = nullptr;
    }

    bool isCallableReturn() const {
        if (this->rt.trigger == RtStageKind::NONE)
            return false;
        assert(this->rt.trigger == RtStageKind::CALLABLE);
        return rt.hitAttribute == nullptr;
    }
    void prepareReturn() {
        assert(this->rt.trigger == RtStageKind::CALLABLE);
        rt.hitAttribute = nullptr;
    }

    // Stages may invoke callable shaders without using an explicit acceleration struct, however, if the stage called
    // from has some acceleration struct, that is what should be used to initialize builtins and what not.
    AccelStruct* getFromAs() const {
        return this->fromAs;
    }

    unsigned getRtIndex() const {
        return this->rt.index;
    }
    // Modify the result by using `copyFrom` (as necessary)
    Value* getRtResult() const {
        return this->rt.result;
    }
    Value* getHitAttribute() const {
        return this->rt.hitAttribute;
    }
    // Unlike rt result, we cannot merely copy the hit attribute since it is output only (for intersection), and thus we
    // don't necessarily have a starting value.
    void setHitAttribute(Value* hit_attrib) {
        this->rt.hitAttribute = hit_attrib;
    }
    AccelStruct* getAccelStruct() const {
        return this->rt.as;
    }

    void setRtData(DataView& view) {
        this->rt.data = &view;
    }
    DataView* getRtData() const {
        return this->rt.data;
    }
};
