/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_IMAGE_HPP
#define VALUES_IMAGE_HPP

#include <cstdint>

#include <array>
#include <string>
#include <tuple>
#include <vector>

#include "aggregate.hpp"
#include "type.hpp"
#include "value.hpp"

class Image final : public Value {
    /// Dimensions of the image:
    /// - xx is width
    /// - yy is height
    /// - zz is depth
    /// - ww is number of array elements (not currently supported)
    unsigned xx, yy, zz;

    /// @brief The number of mipmap levels, which decrease in level of detail (LOD).
    /// Each mipmap has half the dimensions of the prior (truncating as needed except when dividing 1). Fields xx, yy,
    /// and zz determine the dimensions of the mipmap with the most detail (index 0). The number of mipmaps must not be
    /// less than 1, nor should it exceed the value of `log2(max(dim.xyz)) + 1`.
    unsigned mipmaps;

    /// @brief condensed image data, where typically a set of four elements is a single pixel.
    /// The format determines the type of the image data, so we don't need/want to store that info for each component
    /// of every pixel (which is needlessly wasteful). However, this means that we must reinterpret cast the data for
    /// every use.
    /// Image provides two options to provide texel data:
    /// 1) an image file
    /// 2) a data array
    /// Only one may be provided at a time. Where one is provided, the other must be empty. Internally, both resolve to
    /// this flat data vector.
    std::vector<uint32_t> data;

    /// @brief A definition for how flat data and pixels correspond to each other
    /// Each of the first four members must be some value 0-4, with no repeats, excepting 0, which indicates disablement
    /// The last member, count, must be the number of elements previous which are nonzero
    struct Component {
        unsigned r;
        unsigned g;
        unsigned b;
        unsigned a;
        unsigned count;

        /// @param in the input value to parse from
        /// @param check whether extra checking should be done. Should be false if we expect `in` is valid
        Component(unsigned in, bool check);

        unsigned& operator[](unsigned index);

        unsigned operator[](unsigned index) const;

        void assertCompatible(const Component& other);

        friend std::ostream& operator<<(std::ostream& os, const Component& comp) {
            if (comp.count == 0)
                os << "Unknown";
            else {
                for (unsigned i = 1; i < 4; ++i) {
                    if (comp.r == i)
                        os << 'R';
                    else if (comp.g == i)
                        os << 'G';
                    else if (comp.b == i)
                        os << 'B';
                    else if (comp.a == i)
                        os << 'A';
                }
            }
            return os;
        }
    };
    /// @brief the format of how pixel components are represented in `data`
    Component comps;

    /// @brief a path to an image file or the empty string.
    /// An image can have up to three dimensions. Below, a data encoding pattern is described for each dimensionality:
    /// 1D) Pixels in a single mipmap are expected from left to right. The left side of each mipmap is placed at the
    /// next available corner closest to the top-left image corner. For example, with 4 mipmaps of a size 8 image:
    /// (0)              -> +x
    ///   0 0 0 0 0 0 0 0
    ///   1 1 1 1 2 2 3 -
    ///
    /// 2D) The top-left corner of each mipmap level is placed at the next available corner closest to the top-left
    /// image corner. For example, with 4 mipmaps of an 8x8 image:
    /// (0, 0)                    -> +x
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 2 2 3 -
    ///   0 0 0 0 0 0 0 0 2 2 - -
    ///   0 0 0 0 0 0 0 0 - - - -
    ///   0 0 0 0 0 0 0 0 - - - -
    /// |
    /// v +y
    ///
    /// 3D) xy layers are placed horizontally in ascending z order. The top-left corner of each mipmap level is placed
    /// at the next available corner closest to the top-left image corner. For example, consider a 4x4x4 image with 3
    /// mipmaps, where each pixel is denoted [mipmap level][z index]:
    /// (0, 0, 0)                                                    -> +x, +z
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 10 10 11 11
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 10 10 11 11
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 20 -- -- --
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 -- -- -- --
    /// |
    /// v +y
    std::string reference;
    bool fromFile = false;  // this is a hint for whether to write it out

    // TODO I suspect that there are a couple extra fields which need to be encoded in the type. As far as I can tell,
    // these only really apply to floats, but we probably need to track them to get correct output
    // 1) Normalization: whether the range spans 255 or 1
    // 2) Signedness: whether half the range is negative or all in positive
    // Of course, the two options are orthogonal. We can have a non-normalized, positively signed range of [0.0, 255.0]
    // or a normalized negatively signed range of [-0.5, 0.5].

    inline static const std::vector<std::string> names {"ref", "dim", "mipmaps", "comps", "data"};

    /// @brief Returns a value corresponding to an out-of-bounds image access.
    /// This value is vendor-specific, ie, not defined by the spec. All zeros is common in practice.
    [[nodiscard]] Array* outOfBoundsAccess() const;

    /// @brief Decomposes the float value into an unsigned int base and a float ratio
    ///
    /// The unsigned int base is an unsigned int with the int part of val.
    /// The ratio is how close the original value is to the next int of larger magnitude
    ///
    /// For example:
    /// - decompose(1.0)  = {1, 0.0}
    /// - decompose(3.4)  = {3, 0.4}
    static std::tuple<unsigned, float> decompose(float val);

public:
    Image(Type t) : Value(t), xx(0), yy(0), zz(0), comps(t.getComps(), false) {};

    bool equals(const Value& val) const override;

    /// @brief Copy the image's fields from the other struct, if possible
    /// @param other the struct to copy from
    void copyFrom(const Struct& str) noexcept(false);

    void copyFrom(const Value& new_val) noexcept(false) override;

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to image!");
    }

    // Here is what an image looks like in YAML:
    // img :
    //   ref : <string>
    //   dim : <uvec3, uvec2, or uint>
    //   mipmaps : <uint>
    //   comps : <uint>
    //   data :
    //   - float, int, or uint, as long as it is consistent
    //   - <...>
    Struct* toStruct() const;

    unsigned getDimensionality() const {
        return type.getDim();
    }

    /// @brief Get the size of the image at the given LOD level
    /// @param lod the level of detail to query. 0 is the most detailed level
    /// @return a tuple of four floats: width, height, depth, and number of array elements.
    ///
    /// Note, some images are of a dimensionality which don't specify some of the return floats. For all missing
    /// component(s), an undefined returned and the output is expected to be truncated in dimension to match with
    /// #getDimensionality().
    std::array<unsigned, 4> getSize(uint32_t lod = 0) const;

    static std::tuple<float, float, float, float> extractCoords(const Value* coords_v, unsigned dim, bool proj);

    [[nodiscard]] Array* read(float x, float y, float z, float lod) const;

    bool write(int x, int y, int z, const Array& texel);
};
#endif
