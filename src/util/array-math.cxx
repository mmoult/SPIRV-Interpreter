/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <functional>
#include <set>
#include <vector>

#include "../values/type.hpp"

export module util.arraymath;
import value.aggregate;
import value.primitive;
import value.statics;

export namespace ArrayMath {

double dot(const Array& first, const Array& second) {
    assert(first.getSize() == second.getSize());
    double dot_product = 0.0;
    for (unsigned i = 0; i < first.getSize(); ++i) {
        const float second_elem = static_cast<const Primitive*>(second[i])->data.fp32;
        const float this_elem = static_cast<const Primitive*>(first[i])->data.fp32;
        dot_product += second_elem * this_elem;
    }
    return dot_product;
}

double dot(const Value* first, const Value* second) {
    const Value& v0 = *first;
    const Value& v1 = *second;

    const Type& v0t = v0.getType();
    const Type& v1t = v1.getType();

    if (v0t.getBase() == DataType::FLOAT) {
        assert(v1t.getBase() == DataType::FLOAT);
        double d = static_cast<const Primitive&>(v0).data.fp32;
        return d * static_cast<double>(static_cast<const Primitive&>(v1).data.fp32);
    }

    assert(v0t.getBase() == DataType::ARRAY && v1t.getBase() == DataType::ARRAY);
    assert(v0t.getElement().getBase() == DataType::FLOAT && v1t.getElement().getBase() == DataType::FLOAT);
    return dot(static_cast<const Array&>(v0), static_cast<const Array&>(v1));
}

double determinant(const Array& arr) {
    // This must be a square matrix of floating point components
    unsigned size = arr.getSize();
    assert(size > 0 && "Cannot find the determinant of an empty matrix!");
    const Value* el = arr[0];
    const Type& el_type = el->getType();
    assert(
        (el_type.getBase() == DataType::ARRAY && el_type.getSize() == size) &&
        "Cannot compute determinant for non-square matrix!"
    );
    assert(
        el_type.getElement().getBase() == DataType::FLOAT &&
        "Base type of square matrix must be float to compute determinant!"
    );
    assert(size > 1 && "Cannot find the determinant of a 1x1 matrix!");

    std::vector<double> matrix;  // matrix transposed
    matrix.reserve(size * size);
    // Load all elements into a matrix list for easier reference
    for (unsigned i = 0; i < size; ++i) {
        const auto& column = static_cast<const Array&>(*arr[i]);
        for (unsigned j = 0; j < size; ++j)
            matrix.push_back(static_cast<const Primitive*>(column[j])->data.fp32);
    }

    // Remember that the matrix is transposed, so we flip x and y in calculation
#define GET(X, Y) matrix[(X) * size + (Y)]

    std::function<double(std::set<unsigned>&)> determinant_rec = [&](std::set<unsigned>& col_skips) {
        unsigned subsize = size - col_skips.size();
        if (subsize == 2) {
            // ⎡ a b ⎤
            // ⎣ c d ⎦
            // ad - bc
            unsigned x0, x1;
            bool first = true;
            for (unsigned i = 0; i < size; ++i) {
                if (!col_skips.contains(i)) {
                    if (first) {
                        x0 = i;
                        first = false;
                    } else {
                        x1 = i;
                        break;
                    }
                }
            }
            unsigned y0 = size - subsize;
            double a = GET(x0, y0);
            double b = GET(x1, y0);
            double c = GET(x0, y0 + 1);
            double d = GET(x1, y0 + 1);
            return (a * d) - (b * c);
        } else {
            // ⎡ a b c ⎤
            // ⎢ d e f ⎥
            // ⎣ g h i ⎦
            // (a * det(skips ∪ col 0)) - (b * det(skips ∪ col 1)) + (c * det(skips ∪ col 2)) - ...
            unsigned terms = 0;
            double sum = 0.0;
            for (unsigned i = 0; i < size; ++i) {
                if (terms > subsize)
                    break;
                if (col_skips.contains(i))
                    continue;
                // If we got here, we successfully found a term to evaluate
                ++terms;
                // Create a new skips set, skipping the current column
                std::set<unsigned> new_skips(col_skips);
                new_skips.insert(i);
                double top = GET(i, size - subsize);  // a, b, or c from the earlier example
                if (terms % 2 == 0)
                    top = -top;  // Negate every other term
                sum += (top * determinant_rec(new_skips));
            }
            return sum;
        }
    };
#undef GET

    std::set<unsigned> no_skips;
    return determinant_rec(no_skips);
}

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
