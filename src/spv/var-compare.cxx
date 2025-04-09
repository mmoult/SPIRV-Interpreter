/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstring>
#include <optional>
#include <string>

#include "../../external/spirv.hpp"
export module spv.varCompare;
import spv.data.data;

export class VarCompare {
    std::string name;
    bool byName = true;
    unsigned binding = Variable::getUnset();
    unsigned set = binding;
    bool buffer = false;

public:
    VarCompare(const std::string& name) : name(name) {}

    void init() {
        if (auto len = name.length(); len > 0 && name[0] == '@' && (len < 2 || name[1] != '@')) {
            byName = false;
            // Try to parse out location data
            const char* name_at = name.c_str() + 1;  // to avoid the first @
            const char* end = name.c_str() + len;
            while (name_at < end) {
                if (auto got = parseDescriptor("location", &name_at, end); got.has_value()) {
                    assert(binding == Variable::getUnset());
                    binding = got.value();
                    buffer = false;
                } else if (auto got = parseDescriptor("binding", &name_at, end); got.has_value()) {
                    assert(binding == Variable::getUnset());
                    binding = got.value();
                    buffer = true;
                } else if (auto got = parseDescriptor("set", &name_at, end); got.has_value()) {
                    assert(set == Variable::getUnset());
                    set = got.value();
                } else {
                    // Unrecognized name. Exit early
                    byName = true;
                    break;
                }
            }
        }
    }

    std::optional<unsigned> parseDescriptor(std::string name, const char** from, const char* end) {
        unsigned length = name.length();
        if (*from + length > end || strncmp(*from, name.c_str(), length) != 0)
            return {};
        (*from) += length;

        unsigned val = 0;
        const char* before = *from;
        while (*from < end) {
            char c = **from;
            if (c >= '0' && c <= '9') {
                val *= 10;
                val += (c - '0');
                ++(*from);
            } else
                break;
        }
        if (before == *from) {
            (*from) -= length;  // reject progress made before
            return {};  // no value found!
        }
        return {val};
    }

    bool isMatch(const Variable& var) {
        if (byName) {
            std::string compare = mangleName(var.getName());
            return (compare == name);
        } else if (var.getBinding() == binding && var.getDescriptorSet() == set) {
            return (buffer == isBuffer(var));
        }
        return false;
    }

    static bool isBuffer(const Variable& var) {
        auto storage = var.getStorageClass();
        return (storage != spv::StorageClassInput && storage != spv::StorageClassOutput);
    }

    static std::string mangleName(const std::string& name) {
        std::string result = name;
        if (!result.empty() && result[0] == '@')
            result = '@' + result;
        return result;
    }
};
