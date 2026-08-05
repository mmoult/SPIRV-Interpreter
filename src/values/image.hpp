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
    /// @brief The extent of each spatial axis at mipmap level 0: xx is width, yy is height, zz is depth.
    /// Only as many of these as Type::getSpatialDims() reports are meaningful; the rest stay 0 rather than 1, so any
    /// arithmetic over all three has to clamp (see #texelsAt). For a cube map, xx and yy are the extent of a single
    /// face, which is square.
    unsigned xx, yy, zz;

    /// @brief How many independent images the data holds, each with a complete mipmap chain of its own.
    /// This counts *storage* layers, which is not the same as what a coordinate indexes: a cube map stores six layers
    /// per array element, one per face, and #arrayLength divides that back out. An image with neither the Arrayed flag
    /// nor a cube dimensionality has exactly one layer.
    ///
    /// Layers differ from the spatial axes in two ways that the rest of this class depends on. They are never
    /// interpolated between, because adjacent layers hold unrelated images rather than neighboring texels, and their
    /// count does not shrink with the mipmap level: level 3 of a 16-layer image still has 16 layers.
    unsigned layers;

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
    std::vector<uint64_t> data;

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

    /// @brief The factor each spatial axis is divided by at the given mipmap level.
    /// Level n is half the size of level n-1, and level n is twice the size of level n+1.
    static unsigned mipDivisor(unsigned lod);

    /// @brief How many texels the given mipmap level holds.
    unsigned texelsAt(unsigned lod) const;

    /// @brief The index, within one mipmap chain, of the first element of the given level.
    /// Levels are stored in order of decreasing detail, so a level begins after every level before it. Passing
    /// #mipmaps therefore yields the size of the entire chain.
    unsigned lodOffset(unsigned lod) const;

    /// @brief How many elements one layer occupies, which is its whole mipmap chain.
    /// Layers are stored back to back, so layer n begins at n * layerStride(). Note the consequence for anything that
    /// wants a single mipmap level across all layers: that region is not contiguous.
    unsigned layerStride() const;

    /// @brief How many layers a coordinate can address, which is #layers with a cube map's six faces factored out.
    /// This is the count the shader sees, and so what textureSize reports and what a coordinate's layer component is
    /// bounded by. A cube map's faces are storage layers but not array elements.
    unsigned arrayLength() const;

    /// @brief Converts a coordinate's array index into the index of the first storage layer it names.
    ///
    /// Rounds rather than interpolating, since neighboring layers are unrelated images, and clamps into range. Both
    /// follow the Vulkan specification: the section "(u,v,w,a) to (i,j,k,l,n) Transformation and Array Layer Selection"
    /// of docs.vulkan.org/spec/latest/chapters/textures.html resolves the array coordinate to a layer by rounding to
    /// the nearest even integer and then clamping against the layer count. std::nearbyint is that rounding under the
    /// default rounding mode, where std::round is not.
    ///
    /// For a cube map, one array element spans six consecutive layers, so it returns the first layer of the element
    /// the coordinate selects. The specific face lies at that layer plus its face index.
    unsigned layerIndex(float layer) const;

public:
    /// @brief Where in an image an operation reads or writes, as its coordinate and qualifier operands describe it.
    /// Which members the coordinate actually supplies depends on the image type, in the order they appear here; see
    /// Type::getCoordCount(). Whatever the level of detail says, the spatial coordinates are always in the scale of
    /// mipmap level 0.
    struct Location {
        float x = 0.0;
        float y = 0.0;
        float z = 0.0;
        /// @brief The array element to read from, used only for an arrayed image.
        float layer = 0.0;
        /// @brief The projective divisor, used only for the Proj variants, after every other component.
        /// Instruction::calcImageLocation consumes it: x, y, and z are divided through before the Image sees them.
        float q = 0.0;
        /// @brief The mipmap level of detail, which comes from the instruction's qualifiers, not the coordinate.
        float lod = 0.0;
    };

    // mipmaps defaults to the documented minimum of 1, and layers to the one every image has. copyFrom(const Struct&)
    // overwrites both, but toStruct() and the LOD clamp in read() can each run on an Image that Type::construct() only
    // default-constructed.
    Image(Type t) : Value(t), xx(0), yy(0), zz(0), layers(1), mipmaps(1), comps(t.getComps(), false) {};

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
    //
    // "dim" holds one component per spatial axis, and one more at the end when the image is arrayed. Three components
    // is therefore the most it ever has, since the only dimensionality with three spatial axes is the one Arrayed is
    // forbidden on. That trailing component is the number of array elements, not the number of stored layers, so a cube
    // map's six faces are left implicit: an arrayed cube map of three elements writes [w, h, 3] rather than [w, h, 18],
    // and a lone cube map writes [w, h] with the six understood.
    Struct* toStruct() const;

    /// @brief How many components are expected for a coordinate targeting a single texel in this image. This is NOT
    /// necessarily the number of storage axes. For example, A cube map reports three (a direction vector) over two
    /// spatial axes, and an arrayed image reports one more than its non-arrayed equivalent.
    unsigned getCoordCount() const {
        return type.getCoordCount();
    }

    /// @brief Get the size of the image at the given LOD level
    /// @param lod the level of detail to query. 0 is the most detailed level
    /// @return the extent of each spatial axis, followed by the number of array elements if the image is arrayed.
    ///
    /// The components are packed from the front with no gaps, so which slot means what depends on the image type: the
    /// array length of a 2D arrayed image lands in slot 2, where the depth of a 3D image would be. The caller is
    /// expected to take exactly Type::getSpatialDims() + Type::isArrayed() of them, which is how many components the
    /// result type of an OpImageQuerySize* has; any slot past that is left 0 and means nothing.
    ///
    /// The array length is reported whole at every level of detail. Mipmapping subdivides the spatial axes, never the
    /// layers, so textureSize on a cube array counts cubes rather than faces and does not halve.
    std::array<unsigned, 4> getSize(uint32_t lod = 0) const;

    /// @brief Splits an operation's coordinate operand into the roles its components play.
    /// @param coords_v the coordinate operand, either a scalar or an array of them
    /// @param img_type the type of the image being accessed, which decides how many components mean what
    /// @param proj whether the instruction is a Proj variant, which appends a divisor after everything else
    static Location extractCoords(const Value* coords_v, const Type& img_type, bool proj);

    /// @brief Gets the (interpolated) pixel value at the given location.
    [[nodiscard]] Array* read(const Location& loc) const;

    /// @brief Writes a texel at whole coordinates. The layer, like the coordinates, must name an existing element.
    bool write(int x, int y, int z, int layer, const Array& texel);

    /// @brief Decomposes the float value into an unsigned int base and a float ratio
    ///
    /// The unsigned int base is an unsigned int with the int part of val.
    /// The ratio is how close the original value is to the next int of larger magnitude
    ///
    /// For example:
    /// - decompose(1.0)  = {1, 0.0}
    /// - decompose(3.4)  = {3, 0.4}
    static std::tuple<unsigned, float> decompose(float val);
};
#endif
