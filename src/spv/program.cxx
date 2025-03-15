/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

#include "glm/ext.hpp"

#include "data/manager.h"
#include "../../external/spirv.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "../values/raytrace/trace.hpp"
export module spv.program;
import format.parse;
import front.debug;
import spv.data.data;
import spv.frame;
import spv.instList;
import spv.instruction;
import spv.raySubstage;
import value.aggregate;
import value.primitive;
import value.raytrace.accelStruct;
import value.raytrace.shaderBindingTable;

export class Program {
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
    unsigned localInvocId  = 0;
    unsigned globalInvocId = 0;
    unsigned workGroupSize = 0;

    std::vector<RayTraceSubstage> misses;
    std::vector<RayTraceSubstage> hits;
    std::vector<RayTraceSubstage> callables;

    /// @brief Parses instructions from the binary words.
    /// Should identify whether the whole program is valid before any instructions are executed.
    class ProgramLoader {
        uint8_t* buffer;
        int length;
        /// @brief endianness of the program. true = big, false = little
        bool endian;
        int idx;

        bool determineEndian() {
            // The first four bytes are the SPIR-V magic number
            // Determines the endianness of the program
            uint32_t magic;
            if (!getWord(magic))
                return false;
            if (magic == spv::MagicNumber)
                return true;
            // If the number fetched didn't match, try reversing the endianness and fetching again
            endian = !endian;
            idx -= 4;
            getWord(magic);
            return magic == spv::MagicNumber;
        }

        bool getWord(uint32_t& res) {
            if (idx + 4 > length)
                return false;

            res = 0;
            if (endian) {
                for (int i = idx + 4; idx < i; ++idx) {
                    res = res << 8;
                    res += buffer[idx];
                }
            } else {
                idx += 3;
                for (int i = 0; i < 4; ++i) {
                    res = res << 8;
                    res += buffer[idx - i];
                }
                ++idx;
            }
            return true;
        }

        /// Skip ahead by delta (in words)
        bool skip(int delta) {
            delta *= 4;
            if (idx + delta >= length)
                return false;
            idx += delta;
            return true;
        }

    public:
        ProgramLoader(uint8_t* buffer, int length): buffer(buffer), length(length), endian(true), idx(0) {}

        uint32_t parse(std::vector<Instruction>& insts) noexcept(false) {
#define REQUIRE(COND, MSG) \
if (!(COND)) \
    throw std::runtime_error(MSG);

            REQUIRE(determineEndian(), "Corrupted binary! Magic number missing.");
            REQUIRE(skip(2), "Corrupted binary! Version and/or generator missing.");

            uint32_t bound;
            if (!getWord(bound))
                throw std::runtime_error("Corrupted binary! Missing bound.");

            REQUIRE(skip(1), "Corrupted binary! Missing reserved word.");

            while (idx < length) {
                // Each instruction is at least 1 word = 32 bits, where:
                // - high bits = word count
                // - low bits = opcode
                uint32_t control;
                REQUIRE(getWord(control), "Corrupted binary! Missing instruction control word.");
                uint16_t word_count = control >> 16;
                REQUIRE(word_count >= 1, "Corrupted binary! Word count for instruction less than 1.");
                uint16_t opcode = control & 0xffff;

                std::vector<uint32_t> words;
                for (; word_count > 1; --word_count) { // first word in count is the control (already parsed)
                    uint32_t word;
                    REQUIRE(getWord(word), "Corrupted binary! Missing data in instruction stream!");
                    words.push_back(word);
                }

                Instruction::readOp(insts, opcode, words);
            }

#undef REQUIRE
            return bound;
        }
    };

    ValueMap getVariables(const std::vector<unsigned>& vars) const {
        ValueMap ret;
        for (const auto v : vars) {
            const auto var = data.getGlobal()[v].getVariable();
            ret.emplace(var->getName(), var->getVal());
        }
        return ret;
    }

    unsigned init(ValueMap& provided, DataView& global, RayTraceSubstage* stage, bool single_invoc) {
        unsigned entry = 0;
        std::vector<unsigned>& ins = (stage == nullptr)? this->ins : stage->ins;
        std::vector<unsigned>& outs = (stage == nullptr)? this->outs : stage->outs;
        std::vector<unsigned>& specs = (stage == nullptr)? this->specs : stage->specs;

        auto process_visible_io = [&](const Instruction& inst) {
            inst.ioGen(global, ins, outs, specs, provided, insts[entry]);
        };

        Instruction::DecoQueue decorations(insts.getInstructions());
        unsigned location = insts.getLastBreak();
        unsigned local_idx_loc = 0;
        unsigned local_id_loc = 0;
        unsigned global_id_loc = 0;
        bool entry_found = false; // whether the entry instruction has been found
        bool static_ctn = true; // whether we can construct results statically (until first OpFunction)
        for (; location < insts.size(); ++location) {
            Instruction& inst = insts[location];
            auto opcode = inst.getOpcode();

            if (static_ctn || inst.isStaticDependent()) {
                if (opcode == spv::OpFunction) {
                    // Static construction is no longer legal at the first non-static
                    static_ctn = false;

                    if (!entry_found)
                        break;
                    // OpFunction is static dependent, so intended fallthrough
                }

                // silently ignore all but the first entry found
                // (I think it is legal to have multiple- maybe add a way to distinguish desired?)
                if (opcode == spv::OpEntryPoint && !entry_found) {
                    entry_found = true;
                    entry = location;
                }

                // Process the instruction as necessary
                // If it has a static result, let it execute now on the data vector
                if (!inst.queueDecoration(data.getBound(), location, decorations)) {
                    inst.makeResult(global, location, &decorations);

                    if (static_ctn) {
                        // Some builtins need to be removed from the interface, in which case they continue,
                        // others just need to report results, in which they can be saved and break.
                        if (stage == nullptr) {
                            switch (inst.getVarBuiltIn(global)) {
                            case spv::BuiltIn::BuiltInLocalInvocationIndex:
                            case spv::BuiltIn::BuiltInInvocationId:
                                localInvocIdx = inst.getResult();
                                local_idx_loc = location;
                                continue;
                            case spv::BuiltIn::BuiltInLocalInvocationId:
                                localInvocId = inst.getResult();
                                local_id_loc = location;
                                continue;
                            case spv::BuiltIn::BuiltInGlobalInvocationId:
                                globalInvocId = inst.getResult();
                                global_id_loc = location;
                                continue;
                            case spv::BuiltIn::BuiltInWorkgroupSize:
                                workGroupSize = inst.getResult();
                                break;
                            default:
                                break;
                            }
                        } else {
                            // need to catch some builtins for rt substages here
                            if (stage->handleStaticInst(inst))
                                continue;
                        }

                        process_visible_io(inst);
                    }
                }
            }
        }

        if (single_invoc) {
            // Need to allow specifying the id-observing variables.
            // However, the variables transmit overlapping info- we can deduce local invocation index if we have the id,
            // and we can deduce both if we have the global invocation id. There is no reason to force the user to
            // supply the same data twice (if multiple of these variables exist on the interface)- that is redundant and
            // can lead to unnecessary error. Therefore, select the *least* specific invocation variable used by the
            // shader to be in the shader's template.

            // Since we cannot guarantee the ordering of these variable's declarations, we must cache their locations
            // until all are processed.

            unsigned top_var;
            if (global_id_loc != 0)
                top_var = global_id_loc;
            else if (local_id_loc != 0)
                top_var = local_id_loc;
            else if (local_idx_loc != 0)
                top_var = local_idx_loc;
            else
                top_var = 0;

            // If no id variable is used by the shader, no need to have the user define which invocation to use.
            if (top_var != 0)
                process_visible_io(insts[top_var]);
        }

        if (!entry_found)
            throw std::runtime_error("Program is missing entry function!");
        return entry;
    }

    RayTraceSubstage& getSubstage(RtStageKind stage, Frame& launched_from) {
        unsigned index = launched_from.getRtIndex();
        unsigned updated = index;
        std::vector<RayTraceSubstage>* list = nullptr;
        switch (stage) {
        default: // including NONE
            throw std::runtime_error("Cannot get raytracing substage for unsupported type!");
            break;
        case RtStageKind::ANY_HIT:
            list = &hits;
            updated = index * 3 + 0;
            break;
        case RtStageKind::CLOSEST:
            list = &hits;
            updated = index * 3 + 1;
            break;
        case RtStageKind::INTERSECTION:
            list = &hits;
            updated = index * 3 + 2;
            break;
        case RtStageKind::MISS:
            list = &misses;
            break;
        case RtStageKind::CALLABLE:
            list = &callables;
            break;
        }

        if (updated >= list->size()) {
            std::stringstream err;
            err << "Index " << index << " is out of bounds for raytracing substage \"" << to_string(stage) << "\"!";
            throw std::runtime_error(err.str());
        }
        return (*list)[updated];
    }

    void launchSubstage(RtStageKind stage, std::vector<Frame*>& frame_stack) {
        Frame& launched_from = *frame_stack.back();
        RayTraceSubstage& rt_stage = getSubstage(stage, launched_from);
        // fill in builtins into the data
        AccelStruct& as = *launched_from.getAccelStruct();

        const InstanceNode* instance = nullptr;
        glm::vec2 barycentrics(0.0);
        if (stage != RtStageKind::MISS) {
            Intersection& cand = (stage == RtStageKind::CLOSEST)? as.getCommitted() : as.getCandidate();
            instance = cand.instance;
            barycentrics = cand.barycentrics;
        }
        // the instruction which called launchSubstage is responsible for cleaning up the data too.
        DataView& data = *rt_stage.data->clone();
        launched_from.setRtData(data);
        rt_stage.setUpInputs(data, as, *launched_from.getRtResult(), instance);

        Value* hit_attrib = rt_stage.setUpHitAttribute(stage, data, barycentrics, launched_from.getHitAttribute());
        if (hit_attrib != nullptr) {
            // We should not be generating a new hit attribute if there already was one. The setUpHitAttribute function
            // currently prevents this categorically, but the following assert is a good future-proof for memory safety.
            assert(launched_from.getHitAttribute() == nullptr);
            launched_from.setHitAttribute(hit_attrib);
        }
        // Load from the extra input file
        const EntryPoint& ep = insts[rt_stage.entry].getEntryPoint(data);
        std::vector<Data*> entry_args;  // no formal arguments to the substage's main

        // Note: a frame assumes that it owns its data (and will therefore delete it upons deconstruction). We avoid
        // this problem by making a change to instruction's execute and preventing data delete if the frame before has
        // a raytracing trigger enabled.
        frame_stack.push_back(new Frame(ep.getLocation(), entry_args, 0, data));
    }
    void completeSubstage(RtStageKind stage, Frame& launched_from) {
        RayTraceSubstage& rt_stage = getSubstage(stage, launched_from);
        rt_stage.cleanUp(launched_from);
    }

    /// @brief Copies inputs from the provided map to their matching variables, verifying that inputs match expected.
    /// @param provided map of names to values
    /// @param unused whether it is appropriate for some variables to be missing- in which case, they are filled with
    ///               default values.
    /// @return the shader binding table to process (if any), otherwise, nullptr
    static const ShaderBindingTable* checkInputs(
        ValueMap& provided,
        DataView& global, // needed for fetching variables
        std::vector<unsigned>& ins,
        std::vector<unsigned>& specs,
        bool unused
    ) noexcept(false) {
        // First, create a list of variables needed as inputs
        std::vector<Variable*> inputs;
        for (const auto in : ins)
            // var already checked not null in ioGen
            inputs.push_back(global[in].getVariable());

        // Spec constants are not mandatory in the input file!
        // Although they had their values assigned earlier (and therefore, must not be assigned again), we check them
        // here since their name-value pairs may appear in the input, we must recognize them as valid.
        std::vector<Variable*> specConsts;
        for (const auto spec : specs)
            specConsts.push_back(global[spec].getVariable());

        // Next go through variables defined and verify they match needed
        const ShaderBindingTable* sbt = nullptr;
        for (const auto& [name, val] : provided) {
            bool found = false;
            // first, find the variable which matches the name
            for (unsigned i = 0; i < inputs.size(); ++i) {
                auto var = inputs[i];
                if (var->getName() == name) {
                    found = true;
                    try {
                        var->setVal(*val);
                    } catch (const std::exception& e) {
                        std::stringstream err;
                        err << "Could not copy input variable \"" << name << "\" into program memory: ";
                        err << e.what();
                        throw std::runtime_error(err.str());
                    }

                    // Special case for acceleration structures since their shader binding table may have extra shaders
                    // to parse and resolve.
                    const Value& now_val = *var->getVal();
                    if (now_val.getType().getBase() == DataType::ACCEL_STRUCT &&
                    val->getType().getBase() == DataType::STRUCT) {
                        if (sbt != nullptr)  // Cannot currently handle multiple shader binding tables.
                            throw std::runtime_error(
                                "Encountered multiple accel structures while checking input! At most one may be used."
                            );
                        sbt = &static_cast<const AccelStruct&>(now_val).getShaderBindingTable();
                    }

                    // Remove the interface from the check list
                    inputs.erase(inputs.begin() + i);
                    --i;
                    break;
                }
            }
            if (!found) {
                for (Variable* specConst : specConsts) {
                    if (specConst->getName() == name) {
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                std::stringstream err;
                err << "Input specifies variable \"" << name << "\" which doesn't exist in the program interface!";
                throw std::runtime_error(err.str());
            }
        }

        // At this point, all "in" interfaces should be removed. If not, there are more vars needed not provided
        if (!(inputs.empty() || unused)) {
            std::stringstream error;
            error << "Missing ";
            const auto missing = inputs.size();
            error << missing;
            if (missing == 1)
                error << " variable";
            else
                error << " variables";
            error << " in setup: ";
            for (unsigned i = 0; i < inputs.size(); ++i) {
                if (i > 0)
                    error << ", ";
                error << inputs[i]->getName();
            }
            error << "!";
            throw std::runtime_error(error.str());
        }

        return sbt;
    }

public:
    void parse(std::string file_path, uint8_t* buffer, int length) noexcept(false) {
        // Delegate parsing to a nested loader class. The loader has some fields which are not needed after parsing.
        // This allows for a cleaner separation of data.
        ProgramLoader load(buffer, length);
        insts.addBreak(insts.size(), file_path);
        uint32_t bound = load.parse(insts.getInstructions());
        data.setBound(std::max(bound, data.getBound()));
    }

    unsigned getInstLength() const {
        return insts.size();
    }

    void init(ValueMap& provided, bool single_invoc) noexcept(false) {
        entry = init(provided, data.getGlobal(), nullptr, single_invoc);
    }
    void initRaytrace(RayTraceSubstage& stage, ValueMap& extra_inputs, bool unused = false) {
        unsigned entry = init(extra_inputs, *stage.data, &stage, false);
        stage.entry = entry;
        auto* extra_accel = Program::checkInputs(extra_inputs, *stage.data, stage.ins, stage.specs, unused);
        if (extra_accel != nullptr)  // We shouldn't see any extra accel structs
            throw std::runtime_error("Extra acceleration struct found when invoking raytracing substage!");
    }

    /// @brief Copies inputs from the provided map to their matching variables, verifying that inputs match expected.
    /// @param provided map of names to values
    /// @param unused whether it is appropriate for some variables to be missing- in which case, they are filled with
    ///               default values.
    /// @return the shader binding table to process (if any), otherwise, nullptr
    const ShaderBindingTable* checkInputs(ValueMap& provided, bool unused) noexcept(false) {
        return Program::checkInputs(provided, this->data.getGlobal(), this->ins, this->specs, unused);
    }

    std::tuple<bool, unsigned> checkOutputs(ValueMap& checks) const noexcept(true) {
        // First, create a list of variables from outputs
        std::vector<const Variable*> outputs;
        const auto& global = data.getGlobal();
        for (const auto out : outs) {
            auto var = global[out].getVariable();
            // var already checked not null in ioGen
            outputs.push_back(var);
        }
        unsigned total_tests = outputs.size();

        // Next go through checks and find the corresponding in outputs
        for (const auto& [name, val] : checks) {
            bool found = false;
            // first, find the variable which matches the name
            for (unsigned i = 0; i < outputs.size(); ++i) {
                auto var = outputs[i];
                if (var->getName() == name) {
                    found = true;
                    // Now is the hard part- we need to compare whether this output matches the check file.
                    // The check file lost some type precision (ie 0.0 -> 0), so we assume outputs are the
                    // standard of type truth, although by definition the check values must be correct.
                    // Therefore, we construct a dummy with the output's type and copy values from the check
                    // into it, then compare for equality.
                    const Value* var_val = var->getVal();
                    const auto& v_type = var_val->getType();
                    Value* dummy;
                    try {
                        dummy = v_type.construct();
                        dummy->copyFrom(*val);
                        bool compare = dummy->equals(*var_val);
                        delete dummy;
                        if (!compare) {
                            std::stringstream err;
                            std::cerr << "Output variable \"" << name;
                            std::cerr << "\" did not match the expected value!" << std::endl;
                            return std::tuple(false, total_tests);
                        }
                    } catch(const std::exception& e) {
                        if (dummy != nullptr)
                            delete dummy;
                        return std::tuple(false, total_tests);
                    }
                    // Remove the interface from the compare list
                    outputs.erase(outputs.begin() + i);
                    --i;
                    break;
                } else
                    continue; // this isn't a match, try next
            }

            if (!found)
                return std::tuple(false, total_tests);
        }

        // At this point, all outputs should be removed. If not, there are more outputs than in the check file
        // (which means the output is not equal to the check)
        return std::tuple(outputs.empty(), total_tests);
    }

    void execute(bool verbose, bool debug, ValueFormat& format, bool single_invoc) noexcept(false) {
        Instruction& entry_inst = insts[entry];
        DataView& global = data.getGlobal();

        // Load the workgroup size from the variable, if provided
        if (workGroupSize != 0) {
            const Variable& workSizeVar = *static_cast<const Variable*>(global[workGroupSize].getVariable());
            const Aggregate& sizeAgg = *static_cast<const Aggregate*>(workSizeVar.getVal());
            // Update the entry point
            EntryPoint& ep = entry_inst.getEntryPoint(global);
            ep.sizeX = static_cast<const Primitive*>(sizeAgg[0])->data.u32;
            ep.sizeY = static_cast<const Primitive*>(sizeAgg[1])->data.u32;
            ep.sizeZ = static_cast<const Primitive*>(sizeAgg[2])->data.u32;
        }
        const EntryPoint& ep = entry_inst.getEntryPoint(global);
        unsigned num_invocations = single_invoc? 1 : (ep.sizeX * ep.sizeY * ep.sizeZ);

        Debugger debugger(insts, format, num_invocations);
        // The stack frame holds variables, temporaries, program counter, return address, etc
        // We have a stack frame for each invocation
        std::vector<std::vector<Frame*>> frame_stacks(num_invocations);
        std::vector<DataView*> invoc_globals;
        invoc_globals.reserve(num_invocations);
        std::set<unsigned> active_threads;
        std::set<unsigned> live_threads;
        // afaik, the entry point never takes any arguments
        std::vector<Data*> entry_args;

        Variable* local_invoc_idx = nullptr;
        Variable* local_invoc_id  = nullptr;
        Variable* global_invoc_id = nullptr;
        const Type tUint = Type::primitive(DataType::UINT);
        if (localInvocIdx != 0)
            local_invoc_idx = global[localInvocIdx].getVariable();
        if (localInvocId != 0)
            local_invoc_id = global[localInvocId].getVariable();
        if (globalInvocId != 0)
            global_invoc_id = global[globalInvocId].getVariable();

        for (unsigned i = 0; i < num_invocations; ++i) {
            unsigned local_x = i % ep.sizeX;
            unsigned local_y = (i / ep.sizeX) % ep.sizeY;
            unsigned local_z = (i / (ep.sizeX * ep.sizeY)) % ep.sizeZ;

            DataView* invoc_global = data.makeView(&global);
            invoc_globals.push_back(invoc_global);
            active_threads.insert(i);
            live_threads.insert(i);
            // Copy over builtins from the global scope to the invocation's scope and populate with their values
            if (global_invoc_id != nullptr) {
                // GlobalInvocationID = WorkGroupID * WorkGroupSize + LocalInvocationID
                if (single_invoc) {
                    // Because single invocation was specified and this variable is present, the value must have already
                    // been set in input. We must fetch the value to update the more specific invoc fields- local ID and
                    // local index
                    assert(global_invoc_id->getVal()->getType().getBase() == DataType::ARRAY);
                    const auto& ids = static_cast<const Array&>(*global_invoc_id->getVal());
                    // deconstruct local ids from the given global
                    local_x = static_cast<const Primitive&>(*ids[0]).data.u32 % ep.sizeX;
                    local_y = static_cast<const Primitive&>(*ids[1]).data.u32 % ep.sizeY;
                    local_z = static_cast<const Primitive&>(*ids[2]).data.u32 % ep.sizeZ;
                } else {
                    Variable* v = new Variable(*global_invoc_id);
                    Array arr(tUint, 3);
                    const Primitive gid_x(0 * ep.sizeX + local_x);
                    const Primitive gid_y(0 * ep.sizeY + local_y);
                    const Primitive gid_z(0 * ep.sizeZ + local_z);
                    std::vector<const Value*> elements{&gid_x, &gid_y, &gid_z};
                    arr.addElements(elements);
                    v->setVal(arr);
                    invoc_global->local(globalInvocId).redefine(v);
                }
            }
            if (local_invoc_id != nullptr) {
                if (single_invoc && global_invoc_id == nullptr) {
                    // This is the highest-level invocation builtin. Get the current settings to update any lower
                    assert(global_invoc_id->getVal()->getType().getBase() == DataType::ARRAY);
                    const auto& ids = static_cast<const Array&>(*global_invoc_id->getVal());
                    local_x = static_cast<const Primitive&>(*ids[0]).data.u32;
                    local_y = static_cast<const Primitive&>(*ids[1]).data.u32;
                    local_z = static_cast<const Primitive&>(*ids[2]).data.u32;
                    // TODO: should we throw an error if the local sizes given exceed the workgroup size?
                } else {
                    Variable* v = new Variable(*local_invoc_id);
                    Array arr(tUint, 3);
                    const Primitive gid_x(local_x);
                    const Primitive gid_y(local_y);
                    const Primitive gid_z(local_z);
                    std::vector<const Value*> elements{&gid_x, &gid_y, &gid_z};
                    arr.addElements(elements);
                    v->setVal(arr);
                    invoc_global->local(localInvocId).redefine(v);
                }
            }
            if (local_invoc_idx != nullptr) {
                // The variable should have already been set (and should therefore, not be set again) if single
                // invocation mode is enabled and there are no higher-level variables to preempt.
                if (!single_invoc || (global_invoc_id != nullptr || local_invoc_id != nullptr)) {
                    Variable* v = new Variable(*local_invoc_idx);
                    unsigned index;
                    if (single_invoc)
                        // (gl_LocalInvocationID.z * gl_WorkGroupSize.x * gl_WorkGroupSize.y)
                        // + (gl_LocalInvocationID.y * gl_WorkGroupSize.x)
                        // + gl_LocalInvocationID.x
                        index = (local_z * ep.sizeX * ep.sizeY) + (local_y * ep.sizeX) + local_x;
                    else
                        index = i;
                    const Primitive idx(index);
                    v->setVal(idx);
                    invoc_global->local(localInvocIdx).redefine(v);
                }
            }

            frame_stacks[i].push_back(new Frame(ep.getLocation(), entry_args, 0, *invoc_global));
        }

        // Right now, do something like round robin scheduling. In the future, we will want to give other options
        // through the command line
        unsigned next_invoc = num_invocations - 1;
        while (!live_threads.empty()) {
            if (active_threads.empty()) {
                // All active threads have hit a barrier. Unblock all.
                for (unsigned live : live_threads)
                    active_threads.insert(live);
            }
            ++next_invoc;
            while (!active_threads.contains(next_invoc)) {
                if (next_invoc >= num_invocations)
                    next_invoc = 0;
                else
                    ++next_invoc;
            }

            auto& frame_stack = frame_stacks[next_invoc];
            auto& cur_frame = *frame_stack.back();
            DataView& cur_data = cur_frame.getData();
            unsigned i_at = cur_frame.getPC();
            if (i_at >= insts.size())
                throw std::runtime_error("Program execution left program's boundaries!");

            // Print the line and invoke the debugger, if enabled
            if (verbose)
                debugger.printLine(next_invoc, i_at);
            if (debug) {
                if (debugger.invoke(i_at, cur_data, frame_stack))
                    break;
            }

            unsigned frame_depth = frame_stack.size();
            if (insts[i_at].execute(cur_data, frame_stack, verbose))
                active_threads.erase(next_invoc);

            // print the result if verbose
            if (unsigned result = insts[i_at].getResult();
            // Print the result's value iff:
            // - verbose mode is enabled
            // - the instruction has a result to print
            // - the instruction didn't add or remove a frame (in which case, the value may be undefined)
            verbose && result > 0 && frame_stack.size() == frame_depth) {
                debugger.print(result, cur_data);
            }

            // If the frame stack is empty, the thread has completed (and is no longer alive)
            if (frame_stack.empty()) {
                active_threads.erase(next_invoc);
                live_threads.erase(next_invoc);
                data.destroyView(invoc_globals[next_invoc]);
            } else {
                // If the frame has triggered raytracing, we need to launch the substage
                auto& frame = *frame_stack.back();
                if (auto substage = frame.getRtTrigger(); substage != RtStageKind::NONE) {
                    if (frame_stack.size() == frame_depth)
                        launchSubstage(substage, frame_stack);
                    else
                        completeSubstage(substage, frame);
                }
            }
        }
    }

    ValueMap getInputs() const {
        auto input_map = getVariables(ins);
        auto spec_consts = getVariables(specs);
        input_map.insert(spec_consts.begin(), spec_consts.end());
        return input_map;
    }
    ValueMap getOutputs() const {
        return getVariables(outs);
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
