/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_PROGRAM_HPP
#define SPV_PROGRAM_HPP

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include "../util/spirv.hpp"
#include "glm/ext.hpp"

#include "../format/parse.hpp"
#include "../values/aggregate.hpp"
#include "../values/primitive.hpp"
#include "../values/raytrace/accel-struct.hpp"
#include "../values/raytrace/shader-binding-table.hpp"
#include "../values/raytrace/trace.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "data/data.hpp"
#include "data/manager.hpp"
#include "frame.hpp"
#include "inst-list.hpp"
#include "instruction.hpp"
#include "ray-substage.hpp"
#include "var-compare.hpp"

class Program {
    InstList insts;
    // Entry point information
    unsigned entry;

    DataManager data;
    // We may associate one program with multiple data vectors. Therefore, the program may keep ids, but never data
    // objects directly!
    std::vector<unsigned> ins;
    std::vector<unsigned> outs;
    std::vector<unsigned> specs;
    // builtin variables we need to catch
    unsigned localInvocIdx = 0;
    unsigned localInvocId = 0;
    unsigned globalInvocId = 0;
    unsigned workGroupSize = 0;
    // A list of static vars that need thread-level initialization before beginning main
    std::vector<unsigned> threadVars;

    std::vector<RayTraceSubstage> misses;
    std::vector<RayTraceSubstage> hits;
    std::vector<RayTraceSubstage> callables;
    ShaderBindingTable sbt;

    constexpr static const char* SBT_NAME = "@shader-binding-table";

    /// @brief Parses instructions from the binary words.
    /// Should identify whether the whole program is valid before any instructions are executed.
    class ProgramLoader {
        uint8_t* buffer;
        int length;
        /// @brief endianness of the program. true = big, false = little
        bool endian;
        int idx;

        bool determineEndian();

        bool getWord(uint32_t& res);

        /// Skip ahead by delta (in words)
        bool skip(int delta);

    public:
        ProgramLoader(uint8_t* buffer, int length) : buffer(buffer), length(length), endian(true), idx(0) {}

        uint32_t parse(std::vector<Instruction>& insts) noexcept(false);
    };

    RayTraceSubstage& getSubstage(RtStageKind stage, unsigned index);

    void launchSubstage(RtStageKind stage, std::vector<Frame*>& frame_stack);

    void completeSubstage(RtStageKind stage, Frame& launched_from);

public:
    void parse(std::string file_path, uint8_t* buffer, int length) noexcept(false);

    inline unsigned getInstLength() const {
        return insts.size();
    }

    unsigned init(ValueMap& provided, DataView& global, RayTraceSubstage* stage, bool single_invoc);

    void init(ValueMap& provided, bool single_invoc) noexcept(false) {
        entry = init(provided, data.getGlobal(), nullptr, single_invoc);
    }

    /// @brief Initialize the raytracing substage passed in as stage
    /// Set variables in the stage (which must match the expected type) with the inputs from the main program and from
    /// extra shader record values.
    /// @param stage the stage to initialize
    /// @param expected the expected substage shader type
    /// @param extra_inputs the shader record values, if any
    /// @param unused whether to allow lenient input checking- default values are used if they don't appear in any
    ///               input. (Useful if the variable is unused by the shader logic.)
    void initRaytrace(RayTraceSubstage& stage, spv::ExecutionModel expected, ValueMap& extra_inputs, bool unused);

    const ShaderBindingTable& getShaderBindingTable() const {
        return sbt;
    }

    /// @brief Copies inputs from the provided map to their matching variables, verifying that inputs match expected.
    /// @param provided map of names to values
    /// @param unused whether it is appropriate for some variables to be missing- in which case, they are filled with
    ///               default values.
    void checkInputs(ValueMap& provided, bool unused) noexcept(false);

    std::tuple<bool, unsigned> checkOutputs(ValueMap& checks) const noexcept(true);

    void execute(bool verbose, bool debug, ValueFormat& format, bool single_invoc) noexcept(false);

    ValueMap getVariables(const std::vector<unsigned>& vars, bool prefer_location) const;

    ValueMap getInputs(bool prefer_location) const {
        auto input_map = getVariables(ins, prefer_location);
        auto spec_consts = getVariables(specs, prefer_location);
        input_map.insert(spec_consts.begin(), spec_consts.end());

        // If this is an rgen shader, forcibly add the shader binding table as a value
        if (insts[entry].getShaderStage() == spv::ExecutionModelRayGenerationKHR)
            input_map[SBT_NAME] = sbt.toStruct();

        return input_map;
    }
    ValueMap getOutputs(bool prefer_location) const {
        return getVariables(outs, prefer_location);
    }

    std::map<std::string, spv::StorageClass> getStorageClasses() const {
        std::map<std::string, spv::StorageClass> ret;
        for (const auto v : ins) {
            const auto var = data.getGlobal()[v].getVariable();
            ret.emplace(var->getName(), var->getStorageClass());
        }
        return ret;
    }
    std::map<std::string, spv::BuiltIn> getBuiltIns() const {
        std::map<std::string, spv::BuiltIn> ret;
        for (const auto v : ins) {
            const auto var = data.getGlobal()[v].getVariable();
            ret.emplace(var->getName(), var->getBuiltIn());
        }
        return ret;
    }

    RayTraceSubstage& nextMissRecord() {
        unsigned before = misses.size();
        misses.resize(before + 1);
        return misses[before];
    }
    RayTraceSubstage& nextHitRecord() {
        unsigned before = hits.size();
        hits.resize(before + 1);
        return hits[before];
    }
    RayTraceSubstage& nextCallableRecord() {
        unsigned before = callables.size();
        callables.resize(before + 1);
        return callables[before];
    }
    DataManager& getDataManager() {
        return data;
    }
};
#endif
