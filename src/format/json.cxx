/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <sstream>
#include <string>

#include "../values/value.hpp"
export module format.json;
import format.parse;

export class Json : public ValueFormat {
private:

protected:
    void verifyBlank(LineHandler& handle) noexcept(false) override {
        //
    }

    void parseFile(ValueMap& vars, LineHandler& handler) override {
        //
    }

    void parseValue(ValueMap& vars, std::string& key, LineHandler& handle) noexcept(false) override {
        //
    }

public:
    void printFile(std::stringstream& out, const ValueMap& vars) override {

    }
};
