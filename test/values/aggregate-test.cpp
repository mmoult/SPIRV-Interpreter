/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <bit>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../../src/values/aggregate.hpp"
#include "../../src/values/primitive.hpp"
#include "../../src/values/type.hpp"

namespace {

/// @brief Build an array of uint primitives of the given precision.
Array* makeUintArray(unsigned precision, const std::vector<uint64_t>& vals) {
    static std::vector<Type*> kept;  // the array borrows its element type, so it must outlive the array
    Type* el = new Type(Type::primitive(DataType::UINT, precision));
    kept.push_back(el);
    auto* arr = new Array(*el, vals.size());
    std::vector<Value*> elements;
    elements.reserve(vals.size());
    for (uint64_t v : vals)
        elements.push_back(new Primitive(v, precision));
    arr->setElementsDirectly(elements);
    return arr;
}

std::vector<uint64_t> rawOf(const Array& arr) {
    std::vector<uint64_t> out;
    for (unsigned i = 0; i < arr.getSize(); ++i)
        out.push_back(static_cast<const Primitive*>(arr[i])->getRaw());
    return out;
}

}  // namespace

// The destination element width drives the mask used while repacking. Widths of 32 and 64 were both broken: the mask
// was computed as `((t_bitsize == 64) ? 0 : (1 << t_bitsize)) - 1` in a 32-bit unsigned, so a width of 32 shifted an
// int exactly as wide as the type (undefined, and zero in practice) and a width of 64 kept only the low half.
TEST_CASE("Array::copyReinterp packs narrow elements into wide ones", "[aggregate]") {
    SECTION("uint16[4] -> uint32[2]") {
        Array* src = makeUintArray(16, {0x1100, 0x3322, 0x5544, 0x7766});
        Array* dst = makeUintArray(32, {0, 0});
        dst->copyReinterp(*src);
        // Lower-ordered bits of the source map to lower-numbered destination components.
        CHECK(rawOf(*dst) == std::vector<uint64_t> {0x3322'1100, 0x7766'5544});
        delete src;
        delete dst;
    }

    SECTION("uint16[4] -> uint64[1]") {
        Array* src = makeUintArray(16, {0x1100, 0x3322, 0x5544, 0x7766});
        Array* dst = makeUintArray(64, {0});
        dst->copyReinterp(*src);
        CHECK(rawOf(*dst) == std::vector<uint64_t> {0x7766'5544'3322'1100});
        delete src;
        delete dst;
    }

    SECTION("uint8[4] -> uint32[1]") {
        Array* src = makeUintArray(8, {0x00, 0x01, 0x02, 0x03});
        Array* dst = makeUintArray(32, {0});
        dst->copyReinterp(*src);
        CHECK(rawOf(*dst) == std::vector<uint64_t> {0x0302'0100});
        delete src;
        delete dst;
    }
}

TEST_CASE("Array::copyReinterp splits wide elements into narrow ones", "[aggregate]") {
    SECTION("uint32[2] -> uint16[4]") {
        Array* src = makeUintArray(32, {0x3322'1100, 0x7766'5544});
        Array* dst = makeUintArray(16, {0, 0, 0, 0});
        dst->copyReinterp(*src);
        CHECK(rawOf(*dst) == std::vector<uint64_t> {0x1100, 0x3322, 0x5544, 0x7766});
        delete src;
        delete dst;
    }

    SECTION("uint64[1] -> uint16[4]") {
        Array* src = makeUintArray(64, {0x7766'5544'3322'1100});
        Array* dst = makeUintArray(16, {0, 0, 0, 0});
        dst->copyReinterp(*src);
        CHECK(rawOf(*dst) == std::vector<uint64_t> {0x1100, 0x3322, 0x5544, 0x7766});
        delete src;
        delete dst;
    }

    SECTION("uint64[1] -> uint32[2]") {
        Array* src = makeUintArray(64, {0xDEAD'BEEF'1234'5678});
        Array* dst = makeUintArray(32, {0, 0});
        dst->copyReinterp(*src);
        CHECK(rawOf(*dst) == std::vector<uint64_t> {0x1234'5678, 0xDEAD'BEEF});
        delete src;
        delete dst;
    }
}

// Round-tripping must be the identity, whichever direction is taken first.
TEST_CASE("Array::copyReinterp round-trips", "[aggregate]") {
    const std::vector<uint64_t> original {0xBEEF, 0xDEAD, 0x5678, 0x1234};
    Array* wide = makeUintArray(64, {0});
    Array* narrow = makeUintArray(16, original);
    wide->copyReinterp(*narrow);

    Array* back = makeUintArray(16, {0, 0, 0, 0});
    back->copyReinterp(*wide);
    CHECK(rawOf(*back) == original);

    delete wide;
    delete narrow;
    delete back;
}

// Type::construct(undef) forwards to Aggregate::dummyFill, which used to drop the flag and always fill with dummy
// values. OpConstantNull passes undef=false and requires zeros.
TEST_CASE("Type::construct(false) zero-fills aggregates", "[aggregate]") {
    Type el = Type::primitive(DataType::UINT, 32);
    Type arr_type = Type::array(4, el);

    SECTION("null-initialized array is all zeros") {
        auto* arr = static_cast<Array*>(arr_type.construct(false));
        REQUIRE(arr->getSize() == 4);
        CHECK(rawOf(*arr) == std::vector<uint64_t> {0, 0, 0, 0});
        delete arr;
    }

    SECTION("undef-initialized array is not all zeros") {
        // The dummy value is deliberately eye-catching, so it must differ from the null fill.
        auto* arr = static_cast<Array*>(arr_type.construct(true));
        REQUIRE(arr->getSize() == 4);
        CHECK(rawOf(*arr) != std::vector<uint64_t> {0, 0, 0, 0});
        delete arr;
    }

    SECTION("null-initialized nested array is all zeros") {
        Type inner = Type::array(2, el);
        Type outer = Type::array(2, inner);
        auto* arr = static_cast<Array*>(outer.construct(false));
        REQUIRE(arr->getSize() == 2);
        for (unsigned i = 0; i < 2; ++i) {
            const auto& sub = static_cast<const Array&>(*(*arr)[i]);
            CHECK(rawOf(sub) == std::vector<uint64_t> {0, 0});
        }
        delete arr;
    }
}
