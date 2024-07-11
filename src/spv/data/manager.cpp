/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "manager.h"

bool DataView::contains(unsigned index) const {
    if (data.contains(index))
        return true;
    return prev != nullptr && prev->contains(index);
}

Data& DataView::at(unsigned index) {
    if (index >= src->getBound())
        throw std::runtime_error("Index given out of bounds for data view!");
    return this->operator[](index);
}
const Data& DataView::at(unsigned index) const {
    if (index >= src->getBound())
        throw std::runtime_error("Index given out of bounds for data view!");
    return this->operator[](index);
}

Data& DataView::operator[](unsigned index) {
    // Look for a local address first
    if (data.contains(index))
        return data[index];

    if (prev != nullptr && prev->contains(index))
        return (*prev)[index];

    // If the data cannot be found, create it locally
    return data[index];
}
const Data& DataView::operator[](unsigned index) const {
    if (data.contains(index))
        return data.at(index);

    if (prev != nullptr && prev->contains(index))
        return (*prev)[index];

    return data.at(index);
}

unsigned DataView::getBound() const {
    return src->getBound();
}
