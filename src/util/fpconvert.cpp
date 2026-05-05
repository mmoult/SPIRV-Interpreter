/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "fpconvert.hpp"

#include <cassert>
#include <sstream>

#include "../front/console.hpp"

namespace FpConvert {
double quantize(double in, unsigned prec) {
    if (prec == 64) {
        // No action needed since we emulate as f64
    } else if (prec == 32) {
        return static_cast<double>(static_cast<float>(in));
    } else if (prec == 16) {
        return static_cast<double>(decode_flt16(encode_flt16(in)));
    } else {
        std::stringstream err;
        err << "The interpreter does not yet support float precision " << prec << "!";
        Console::warn(err.str());
    }
    return in;
}

unsigned digits_of_precision(unsigned bits) {
    // width: sig figs (approx. Better accuracy the closer to 0)
    //    16: 3-4
    //    32: 6-9
    //    64: 15-18
    assert(bits >= 16);
    return bits < 32 ? 3 : (bits < 64 ? 6 : 15);
}

};  // namespace FpConvert
