/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_INSTLIST_HPP
#define VALUES_INSTLIST_HPP

#include <string>
#include <vector>

#include "instruction.hpp"

struct FileBreak {
    unsigned instNum;
    std::string filePath;

    FileBreak(unsigned inst_num, const std::string& file_path) : instNum(inst_num), filePath(file_path) {}
};

class InstList {
    std::vector<Instruction> insts;
    std::vector<FileBreak> breaks;

public:
    Instruction& operator[](unsigned index) {
        return insts[index];
    }
    const Instruction& operator[](unsigned index) const {
        return insts[index];
    }

    const std::string* getBreak(unsigned index) const {
        // If we only have one break (or no breaks), don't differentiate files
        if (breaks.size() <= 1)
            return nullptr;

        for (const auto& br : breaks) {
            if (br.instNum == index)
                return &br.filePath;
            // Assuming we add breaks in order (which is true of the current use case), we can abort early if the index
            // count has been passed.
            if (br.instNum > index)
                break;
        }
        return nullptr;
    }
    void addBreak(unsigned index, const std::string& file_path) {
        breaks.emplace_back(index, file_path);
    }

    unsigned size() const {
        return insts.size();
    }

    std::vector<Instruction>& getInstructions() {
        return insts;
    }

    unsigned getLastBreak() const {
        if (breaks.empty())
            return 0;
        return breaks[breaks.size() - 1].instNum;
    }
};
#endif
