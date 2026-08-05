/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <array>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../src/values/aggregate.hpp"
#include "../../src/values/image.hpp"
#include "../../src/values/primitive.hpp"
#include "../../src/values/string.hpp"
#include "../../src/values/type.hpp"

using Catch::Approx;

namespace {

/// @brief A texel type outliving any image built from it here.
/// Type::image stores the address of its texel type without owning it, so the type has to outlive the image. Static
/// storage duration gives that without leaking.
const Type& floatTexel() {
    static const Type t = Type::primitive(DataType::FLOAT, 32);
    return t;
}

/// @brief Builds the struct an image reads its fields out of, in the shape a parsed input file produces.
/// Ownership passes to the caller. See the layout documented on Image::toStruct.
Struct* imageStruct(
    const std::vector<unsigned>& dim,
    unsigned mipmaps,
    unsigned comps,
    const std::vector<double>& data
) {
    std::vector<Value*> dims;
    dims.reserve(dim.size());
    for (unsigned d : dim)
        dims.push_back(new Primitive(d));

    std::vector<Value*> texels;
    texels.reserve(data.size());
    for (double v : data)
        texels.push_back(new Primitive(v));

    std::vector<Value*> fields;
    fields.push_back(new String(""));
    fields.push_back(new Array(dims));
    fields.push_back(new Primitive(mipmaps));
    fields.push_back(new Primitive(comps));
    fields.push_back(new Array(texels));
    return new Struct(fields, {"ref", "dim", "mipmaps", "comps", "data"});
}

/// @brief Builds an image of the given type from fields laid out as an input file would give them.
std::unique_ptr<Image> makeImage(
    ImageDim dim,
    bool arrayed,
    const std::vector<unsigned>& extent,
    unsigned mipmaps,
    unsigned comps,
    const std::vector<double>& data
) {
    auto img = std::make_unique<Image>(Type::image(&floatTexel(), dim, arrayed, comps));
    std::unique_ptr<Struct> fields(imageStruct(extent, mipmaps, comps, data));
    img->copyFrom(*fields);
    return img;
}

/// @brief Reads one channel out of the image at the given location.
double readChannel(const Image& img, const Image::Location& loc, unsigned chan = 0) {
    std::unique_ptr<Array> got(img.read(loc));
    REQUIRE(chan < got->getSize());
    return static_cast<const Primitive*>((*got)[chan])->data.f;
}

/// @brief A single-channel image whose every texel is the index of the layer holding it, plus one.
/// Two mipmap levels, so a layer holds five elements: 2x2 at level 0 and 1x1 at level 1. Level 1 is offset by ten so
/// that reading the wrong level within the right layer is as visible as reading the wrong layer.
std::unique_ptr<Image> layeredImage(ImageDim dim, bool arrayed, unsigned array_length, unsigned layers) {
    std::vector<double> data;
    for (unsigned layer = 0; layer < layers; ++layer) {
        const double v = layer + 1;
        for (unsigned i = 0; i < 4; ++i)  // level 0, 2x2
            data.push_back(v);
        data.push_back(v + 10);  // level 1, 1x1
    }
    std::vector<unsigned> extent {2, 2};
    if (arrayed)
        extent.push_back(array_length);
    return makeImage(dim, arrayed, extent, 2, 1000, data);
}

}  // namespace

// A cube map's coordinate is a direction from the center of the cube, so before it means anything it has to name a
// face. The component of largest magnitude picks the face and the other two, divided by it, give a position on it.
TEST_CASE("cube map face selection", "[image]") {
    SECTION("each axis direction names its own face") {
        // The layer order the six faces sit in, which Vulkan defines and SPIR-V does not.
        REQUIRE(Image::selectCubeFace(1.0, 0.0, 0.0).face == 0);  // +X
        REQUIRE(Image::selectCubeFace(-1.0, 0.0, 0.0).face == 1);  // -X
        REQUIRE(Image::selectCubeFace(0.0, 1.0, 0.0).face == 2);  // +Y
        REQUIRE(Image::selectCubeFace(0.0, -1.0, 0.0).face == 3);  // -Y
        REQUIRE(Image::selectCubeFace(0.0, 0.0, 1.0).face == 4);  // +Z
        REQUIRE(Image::selectCubeFace(0.0, 0.0, -1.0).face == 5);  // -Z
    }

    SECTION("an axis direction lands in the middle of its face") {
        // Both of the other components are zero, so both halves of the remap give one half.
        std::vector<std::array<float, 3>> dirs{
            {1.0, 0.0, 0.0},
            {-1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, -1.0, 0.0},
            {0.0, 0.0, 1.0},
            {0.0, 0.0, -1.0},
        };
        for (const auto& dir : dirs) {
            const Image::CubeFace at = Image::selectCubeFace(dir[0], dir[1], dir[2]);
            REQUIRE(at.s == Approx(0.5));
            REQUIRE(at.t == Approx(0.5));
        }
    }

    // Which of the two remaining components runs across the face and which runs down differs per face, as does their
    // sign, because the faces are oriented as seen from outside the cube. Every direction below tilts by two different
    // amounts so that swapping s with t, or flipping either, would show up.
    SECTION("each face orients its own axes") {
        struct Case {
            float rx, ry, rz;
            unsigned face;
            double s, t;
        };
        std::vector<Case> cases {
            {1.0, -0.5, -0.25, 0, 0.625, 0.75},     // +X: across is -z, down is -y
            {-1.0, -0.5, -0.25, 1, 0.375, 0.75},    // -X: across is +z, down is -y
            {0.25, 1.0, -0.5, 2, 0.625, 0.25},      // +Y: across is +x, down is +z
            {0.25, -1.0, -0.5, 3, 0.625, 0.75},     // -Y: across is +x, down is -z
            {0.25, -0.5, 1.0, 4, 0.625, 0.75},      // +Z: across is +x, down is -y
            {0.25, -0.5, -1.0, 5, 0.375, 0.75},     // -Z: across is -x, down is -y
        };
        for (const Case& c : cases) {
            const Image::CubeFace at = Image::selectCubeFace(c.rx, c.ry, c.rz);
            REQUIRE(at.face == c.face);
            REQUIRE(at.s == Approx(c.s));
            REQUIRE(at.t == Approx(c.t));
        }
    }

    SECTION("a tie resolves toward x, then y") {
        // The specification leaves ties undefined, so this only pins down that the choice is deterministic.
        REQUIRE(Image::selectCubeFace(1.0, 1.0, 0.0).face == 0);
        REQUIRE(Image::selectCubeFace(1.0, 0.0, 1.0).face == 0);
        REQUIRE(Image::selectCubeFace(0.0, 1.0, 1.0).face == 2);
        REQUIRE(Image::selectCubeFace(1.0, 1.0, 1.0).face == 0);
    }

    SECTION("a direction of no length still names somewhere") {
        const Image::CubeFace at = Image::selectCubeFace(0.0, 0.0, 0.0);
        REQUIRE(at.face < 6);
        REQUIRE(at.s == Approx(0.5));
        REQUIRE(at.t == Approx(0.5));
    }
}

// Every layer holds a mipmap chain of its own, laid out one after another, so reaching a layer means skipping whole
// chains rather than striding within one level.
TEST_CASE("layered image data layout", "[image]") {
    SECTION("a layer reads only its own texels") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        for (unsigned layer = 0; layer < 3; ++layer) {
            Image::Location loc;
            loc.layer = layer;
            REQUIRE(readChannel(*img, loc) == Approx(layer + 1));
        }
    }

    SECTION("a layer keeps its own mipmap levels") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        for (unsigned layer = 0; layer < 3; ++layer) {
            Image::Location loc;
            loc.layer = layer;
            loc.lod = 1;
            // Level 1 of the right layer, not level 1 of the first layer nor level 0 of the right one.
            REQUIRE(readChannel(*img, loc) == Approx(layer + 11));
        }
    }

    SECTION("a cube map's faces are its layers") {
        // Six layers, no array, so the face alone selects the layer. Every face holds its own index plus one.
        auto img = layeredImage(ImageDim::CUBE, false, 1, 6);
        const std::array<std::array<float, 3>, 6> dirs {{
            {1.0, 0.0, 0.0},
            {-1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, -1.0, 0.0},
            {0.0, 0.0, 1.0},
            {0.0, 0.0, -1.0},
        }};
        for (unsigned face = 0; face < 6; ++face) {
            Image::Location loc;
            loc.x = dirs[face][0];
            loc.y = dirs[face][1];
            loc.z = dirs[face][2];
            REQUIRE(readChannel(*img, loc) == Approx(face + 1));
        }
    }

    SECTION("an arrayed cube map counts elements, not faces") {
        // Two elements of six faces each. The array length in "dim" is 2 while the data holds twelve layers.
        auto img = layeredImage(ImageDim::CUBE, true, 2, 12);
        for (unsigned element = 0; element < 2; ++element) {
            Image::Location loc;
            loc.y = -1.0;  // -Y, the fourth face
            loc.layer = element;
            REQUIRE(readChannel(*img, loc) == Approx(element * 6 + 4));
        }
    }
}

// Every shape above is square in x and y, which cannot tell a row stride from a column stride: swapping the two is
// invisible. These shapes are deliberately not square.
TEST_CASE("non-square image addressing", "[image]") {
    SECTION("the row stride follows the width") {
        // 4 wide by 2 tall, one channel, two layers. Texel (x, y) of layer L holds L*100 + y*10 + x.
        std::vector<double> data;
        for (unsigned layer = 0; layer < 2; ++layer) {
            for (unsigned y = 0; y < 2; ++y) {
                for (unsigned x = 0; x < 4; ++x)
                    data.push_back(layer * 100 + y * 10 + x);
            }
        }
        auto img = makeImage(ImageDim::D2, true, {4, 2, 2}, 1, 1000, data);
        Image::Location loc;
        loc.x = 3;
        loc.y = 1;
        loc.layer = 1;
        // Striding a row by the height would land two texels short, back inside row 0.
        REQUIRE(readChannel(*img, loc) == Approx(113));
    }

    SECTION("getSize reports the width before the height") {
        auto img = makeImage(ImageDim::D2, false, {4, 2}, 1, 1000, std::vector<double>(8, 0.0));
        const std::array<unsigned, 4> size = img->getSize(0);
        REQUIRE(size[0] == 4);
        REQUIRE(size[1] == 2);
    }
}

// A sampled access resolves its layer by rounding to the nearest even and clamping, which is what Vulkan specifies. A
// direct access instead names a whole layer, and naming one that does not exist is out of bounds.
TEST_CASE("image layer resolution", "[image]") {
    SECTION("a fractional layer rounds to the nearest, ties to even") {
        auto img = layeredImage(ImageDim::D2, true, 4, 4);
        auto at = [&img](float layer) {
            Image::Location loc;
            loc.layer = layer;
            return readChannel(*img, loc);
        };
        REQUIRE(at(1.4f) == Approx(2));  // rounds down to layer 1
        REQUIRE(at(1.6f) == Approx(3));  // rounds up to layer 2
        REQUIRE(at(0.5f) == Approx(1));  // tie, so toward even: layer 0
        REQUIRE(at(1.5f) == Approx(3));  // tie, so toward even: layer 2
    }

    SECTION("a sampled layer clamps rather than failing") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        auto at = [&img](float layer) {
            Image::Location loc;
            loc.layer = layer;
            return readChannel(*img, loc);
        };
        REQUIRE(at(99.0f) == Approx(3));  // the last layer
        REQUIRE(at(-5.0f) == Approx(1));  // the first
    }

    SECTION("a direct layer past the end is out of bounds") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        Image::Location loc;
        loc.access = Image::Access::DIRECT;
        loc.layer = 2;
        REQUIRE(readChannel(*img, loc) == Approx(3));  // the last layer that exists
        loc.layer = 3;
        REQUIRE(readChannel(*img, loc) == Approx(0));  // one past it reads as out of bounds
    }
}

// The reported size packs the spatial axes and then the array length, contiguously, so which slot means what depends on
// the image type. A mipmap level subdivides the axes only; the layers are untouched.
TEST_CASE("image size query", "[image]") {
    SECTION("an arrayed image reports its length after its axes") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        const std::array<unsigned, 4> size = img->getSize(0);
        REQUIRE(size[0] == 2);
        REQUIRE(size[1] == 2);
        REQUIRE(size[2] == 3);
    }

    SECTION("a mipmap level halves the axes but not the length") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        const std::array<unsigned, 4> size = img->getSize(1);
        REQUIRE(size[0] == 1);
        REQUIRE(size[1] == 1);
        REQUIRE(size[2] == 3);
    }

    SECTION("a cube map reports cubes, not faces") {
        auto img = layeredImage(ImageDim::CUBE, true, 2, 12);
        const std::array<unsigned, 4> size = img->getSize(0);
        REQUIRE(size[0] == 2);
        REQUIRE(size[1] == 2);
        REQUIRE(size[2] == 2);  // two cubes, though twelve layers hold them
    }
}

// A level's data begins after every level before it, so the offset compounds as a power of two. Computing it as 2*lod
// agreed only through level 2 and then ran off the end of the image.
TEST_CASE("mipmap level offsets", "[image]") {
    SECTION("a level is found by the total size of the levels before it") {
        // 8x8 with four levels: 64, 16, 4 and 1 texels. Each level is filled with its own index.
        std::vector<double> data;
        for (unsigned lod = 0; lod < 4; ++lod) {
            const unsigned side = 8 >> lod;
            for (unsigned i = 0; i < side * side; ++i)
                data.push_back(lod);
        }
        auto img = makeImage(ImageDim::D2, false, {8, 8}, 4, 1000, data);
        for (unsigned lod = 0; lod < 4; ++lod) {
            Image::Location loc;
            loc.lod = lod;
            REQUIRE(readChannel(*img, loc) == Approx(lod));
        }
    }

    SECTION("a fractional level blends the two around it") {
        std::vector<double> data;
        for (unsigned lod = 0; lod < 4; ++lod) {
            const unsigned side = 8 >> lod;
            for (unsigned i = 0; i < side * side; ++i)
                data.push_back(lod);
        }
        auto img = makeImage(ImageDim::D2, false, {8, 8}, 4, 1000, data);
        Image::Location loc;
        // Levels 1 and 2 hold 1 and 2, so half of each is 1.5. Before the offsets came from one place, this asked for
        // an element past the end of the image: level 2 was looked for at 512 in data 340 long.
        loc.lod = 1.5;
        REQUIRE(readChannel(*img, loc) == Approx(1.5));
    }
}

// Comparing images has to reach every layer. Mipmap 0 of each layer is a separate run rather than one block at the
// front of the data, so a comparison that walks only the front misses all but the first layer.
TEST_CASE("layered image equality", "[image]") {
    SECTION("layers beyond the first are compared") {
        auto a = layeredImage(ImageDim::D2, true, 3, 3);
        auto b = layeredImage(ImageDim::D2, true, 3, 3);
        REQUIRE(a->equals(*b));

        // Differ only in the last layer, which is the furthest into the data.
        std::vector<double> changed;
        for (unsigned layer = 0; layer < 3; ++layer) {
            const double v = (layer == 2) ? 99 : layer + 1;
            for (unsigned i = 0; i < 4; ++i)
                changed.push_back(v);
            changed.push_back(v + 10);
        }
        auto c = makeImage(ImageDim::D2, true, {2, 2, 3}, 2, 1000, changed);
        REQUIRE_FALSE(a->equals(*c));
    }

    SECTION("images of differing layer counts are unequal") {
        auto a = layeredImage(ImageDim::D2, true, 3, 3);
        auto b = layeredImage(ImageDim::D2, true, 2, 2);
        REQUIRE_FALSE(a->equals(*b));
    }
}

// The layer count in "dim" is the number of array elements, so a cube map's six faces per element stay implicit.
TEST_CASE("image serialization", "[image]") {
    SECTION("an arrayed image writes its array length") {
        auto img = layeredImage(ImageDim::D2, true, 3, 3);
        std::unique_ptr<Struct> out(img->toStruct());
        const auto& dim = static_cast<const Array&>(*(*out)[1]);
        REQUIRE(dim.getSize() == 3);
        REQUIRE(static_cast<const Primitive*>(dim[2])->data.u == 3);
    }

    SECTION("a cube map leaves its six faces implicit") {
        auto img = layeredImage(ImageDim::CUBE, true, 2, 12);
        std::unique_ptr<Struct> out(img->toStruct());
        const auto& dim = static_cast<const Array&>(*(*out)[1]);
        REQUIRE(dim.getSize() == 3);
        REQUIRE(static_cast<const Primitive*>(dim[2])->data.u == 2);  // cubes, not the twelve stored faces
    }

    SECTION("a non-arrayed cube map writes only its axes") {
        auto img = layeredImage(ImageDim::CUBE, false, 1, 6);
        std::unique_ptr<Struct> out(img->toStruct());
        const auto& dim = static_cast<const Array&>(*(*out)[1]);
        REQUIRE(dim.getSize() == 2);
    }

    SECTION("what is written can be read back") {
        auto img = layeredImage(ImageDim::CUBE, true, 2, 12);
        std::unique_ptr<Struct> out(img->toStruct());
        Image again(Type::image(&floatTexel(), ImageDim::CUBE, true, 1000));
        again.copyFrom(*out);
        REQUIRE(again.equals(*img));
    }
}
