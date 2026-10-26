/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_FRAME_HPP
#define SPV_FRAME_HPP

#include <stdexcept>

#include "../values/raytrace/accel-struct.hpp"
#include "../values/value.hpp"
#include "data/manager.hpp"

enum RtStageKind { NONE, ANY_HIT, CLOSEST, INTERSECTION, MISS, CALLABLE };
inline const char* to_string(RtStageKind kind) {
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
        assert(false && "Invalid RtStageKind!");
        return "invalid";
    }
}

class Frame {
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
        // The result of an intersection or any hit shader is made for the launch and is owned by the frame until it is
        // taken. The others refer to values which are owned elsewhere.
        Value* result = nullptr;
        // The payload of the trace which launched the substage. Only set when the result is not itself the payload
        // (intersection and any hit), so the substage can still see and update the payload.
        Value* payload = nullptr;
        // The hit attribute is made for an intersection shader. The frame owns it while it is launching an intersection
        // or closest hit shader, and otherwise it refers to the attribute of the frame which launched it.
        Value* hitAttribute = nullptr;
        // For CALLABLE only: false while entering the substage, true once returning from it.
        bool callableReturning = false;
        // the data which is a duplicate of the substage's
        DataView* data = nullptr;
    } rt;

    // The hit attribute of the closest procedural hit so far in the trace which this frame is running. The frame owns
    // it, and it is kept apart from the raytracing state above since that is reset each time a shader is launched.
    Value* committedHitAttribute = nullptr;

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
    ~Frame();

    inline unsigned getPC() const {
        return pc;
    }

    Data& getArg() noexcept(false);

    void incPC() noexcept(false);

    void setPC(unsigned pc) noexcept(false) {
        assert(argCount == args.size() || pc == this->pc);
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
    const DataView& getData() const {
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

    void triggerRaytrace(
        RtStageKind stage,
        unsigned index,
        Value* result,
        Value* hit_attrib,
        AccelStruct& as,
        Value* payload = nullptr
    );
    void triggerCallable(unsigned index, Value* callable, AccelStruct* as);

    void disableRaytrace();

    bool isCallableReturn() const {
        if (this->rt.trigger == RtStageKind::NONE)
            return false;
        assert(this->rt.trigger == RtStageKind::CALLABLE);
        return rt.callableReturning;
    }
    void prepareReturn() {
        assert(this->rt.trigger == RtStageKind::CALLABLE);
        rt.callableReturning = true;
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
    // Take the result from the frame, along with the responsibility to delete it.
    Value* takeRtResult() {
        Value* taken = this->rt.result;
        this->rt.result = nullptr;
        return taken;
    }
    // The payload which the substage reads and updates. For most stages, this is the same as the result.
    Value* getRtPayload() const {
        return (this->rt.payload != nullptr) ? this->rt.payload : this->rt.result;
    }
    Value* getHitAttribute() const {
        return this->rt.hitAttribute;
    }
    // Take the hit attribute from the frame, along with the responsibility to delete it.
    Value* takeHitAttribute() {
        Value* taken = this->rt.hitAttribute;
        this->rt.hitAttribute = nullptr;
        return taken;
    }
    // Keep the hit attribute of the new closest procedural hit, in place of any which was kept before.
    void setCommittedHitAttribute(Value* hit_attrib) {
        delete committedHitAttribute;
        committedHitAttribute = hit_attrib;
    }
    // Take the kept hit attribute from the frame, along with the responsibility to delete it.
    Value* takeCommittedHitAttribute() {
        Value* taken = committedHitAttribute;
        committedHitAttribute = nullptr;
        return taken;
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
#endif
