/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_VALUABLE_HPP
#define VALUES_VALUABLE_HPP

class Value;

class Valuable {
public:
    virtual ~Valuable() = default;

    ///@brief Used to display data requested during debug mode
    [[nodiscard]] virtual Value* asValue() const = 0;
};

#endif
