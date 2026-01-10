/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_ARRAYMATH_HPP
#define UTIL_ARRAYMATH_HPP

#include <cassert>
#include <vector>

#include "../values/aggregate.hpp"
#include "../values/primitive.hpp"
#include "../values/statics.hpp"

namespace ArrayMath {

double dot(const Array& first, const Array& second);

double dot(const Value* first, const Value* second);

double determinant(const Array& arr);

template<typename Mat, unsigned Cols, unsigned Rows>
void value_to_glm(const Array& val, Mat& out, bool extract = false) {
    for (unsigned i = 0; i < Cols; ++i) {
        if (!extract) {
            auto& col = static_cast<const Array&>(*val[i]);
            for (unsigned j = 0; j < Rows; ++j)
                out[i][j] = static_cast<const Primitive*>(col[j])->data.fp32;
        } else {
            std::vector<float> col = Statics::extractVec(val[i], "matrix", Rows);
            for (unsigned j = 0; j < 3; ++j)
                out[i][j] = col[j];
        }
    }
}

template<typename Mat, unsigned Cols, unsigned Rows>
void glm_to_value(const Mat& mat, Array& out) {
    for (unsigned i = 0; i < Cols; ++i) {
        auto& col = static_cast<Array&>(*out[i]);
        for (unsigned j = 0; j < Rows; ++j) {
            Primitive prim(static_cast<float>(mat[i][j]));
            col[j]->copyFrom(prim);
        }
    }
}

};  // namespace ArrayMath
#endif
