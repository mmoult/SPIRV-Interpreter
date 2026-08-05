/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "image.hpp"

#include <algorithm>  // for std::max
#include <bit>  // for std::bit_cast
#include <cmath>  // for std::modf, std::fmod, std::floor, std::nearbyint
#include <sstream>

#include "../../external/stb/stb_image.h"
#include "../../external/stb/stb_image_write.h"
#include "primitive.hpp"
#include "statics.hpp"
#include "string.hpp"

Image::Component::Component(unsigned in, bool check) {
    if (check) {
        if (in == 0)
            throw std::runtime_error("Image component must have at least one channel active! All 0 found.");
        if (in > 4321)
            throw std::runtime_error("Image component exceeds maximum legal value (4321)!");
    }

    count = 0;
    unsigned scale = 1000;
    for (unsigned i = 0; i < 4; ++i) {
        // must init all indices even if in becomes 0 (channels only guaranteed to start at 0 in debug builds)
        unsigned factor = in / scale;
        if (factor > 0) {
            if (check && factor > 4)
                throw std::runtime_error("Image component has digit which exceeds the maximum value (4)!");
            in -= (factor * scale);
            ++count;
        }
        (*this)[i] = factor;
        scale /= 10;
    }

    if (check) {
        // At the very end, make sure there are no repeats and no gaps
        bool digits[] = {false, false, false, false};
        for (unsigned i = 0; i < 4; ++i) {
            unsigned dig = (*this)[i];
            if (dig == 0)
                continue;
            if (dig > count)
                throw std::runtime_error("Image component digit exceeds count maximum!");
            dig--;
            if (digits[dig])
                throw std::runtime_error(
                    "Image component digit is repeated! Cannot have multiple channels at the same index."
                );
            digits[dig] = true;
        }
    }
}

unsigned& Image::Component::operator[](unsigned index) {
    switch (index) {
    case 0:
        return r;
    case 1:
        return g;
    case 2:
        return b;
    case 3:
        return a;
    default:
        throw std::runtime_error("Component indexed with invalid value!");
    }
}

unsigned Image::Component::operator[](unsigned index) const {
    switch (index) {
    case 0:
        return r;
    case 1:
        return g;
    case 2:
        return b;
    case 3:
        return a;
    default:
        throw std::runtime_error("Component indexed with invalid value!");
    }
}

void Image::Component::assertCompatible(const Component& other) {
    if (count == 0) {
        // An unknown format lets the other say which channels are present, but not the order they are stored in.
        // SPIR-V hands image texels to and from a shader in RGBA order, so #data is kept in that order and the channels
        // are permuted to match on the way in.
        unsigned next = 1;
        for (unsigned i = 0; i < 4; ++i)
            (*this)[i] = (other[i] == 0) ? 0 : next++;
        count = next - 1;
        return;
    }

    // It is possible to copy from an image with different component order, but all active channels on one must also be
    // active in the other (bidirectional check).
    for (unsigned i = 0; i < 4; ++i) {
        unsigned n = other[i];
        unsigned t = (*this)[i];
        if ((n == 0) != (t == 0)) {
            std::stringstream err;
            err << "Cannot copy image from another with an incompatible components value! Order of active channels ";
            err << "may vary, but which channels are active must be the same. Attempt to copy ";
            err << other << " to " << *this;
            throw std::runtime_error(err.str());
        }
    }
}

[[nodiscard]] Array* Image::outOfBoundsAccess() const {
    // "See the client API specification for handling of coordinates outside the image."
    // For now, return black on out of bounds
    const Type& el = type.getElement();
    std::vector<Value*> vals(comps.count, nullptr);
    for (unsigned i = 0; i < comps.count; ++i) {
        auto prim = new Primitive(0u);
        prim->cast(el);
        vals[i] = prim;
    }
    return new Array(vals);
}

unsigned Image::mipDivisor(unsigned lod) {
    assert(lod < 32);
    return 1u << lod;
}

unsigned Image::texelsAt(unsigned lod) const {
    const unsigned div = mipDivisor(lod);
    return std::max(xx / div, 1u) * std::max(yy / div, 1u) * std::max(zz / div, 1u);
}

unsigned Image::lodOffset(unsigned lod) const {
    unsigned total = 0;
    for (unsigned i = 0; i < lod; ++i)
        total += comps.count * texelsAt(i);
    return total;
}

unsigned Image::layerStride() const {
    return lodOffset(mipmaps);
}

unsigned Image::arrayLength() const {
    assert(layers > 0);
    // Six faces make up one array element of a cube map, and are not separately addressable by a coordinate.
    const unsigned per_element = (type.getImageDim() == ImageDim::CUBE) ? 6 : 1;
    assert(layers % per_element == 0);
    return layers / per_element;
}

unsigned Image::layerIndex(float layer) const {
    const unsigned length = arrayLength();
    unsigned index = 0;
    if (layer > 0.0) {
        const float rounded = std::nearbyint(layer);
        index = (rounded >= static_cast<float>(length)) ? (length - 1) : static_cast<unsigned>(rounded);
    }
    // layers / length is the storage-layers-per-element stride: 6 for a cube map, 1 otherwise.
    return index * (layers / length);
}

Image::CubeFace Image::selectCubeFace(float rx, float ry, float rz) {
    const float ax = std::fabs(rx);
    const float ay = std::fabs(ry);
    const float az = std::fabs(rz);

    // The major axis picks the face. The two remaining components, once divided by it, are the position on that face,
    // and which of them runs across and which runs down differs per face: the faces are oriented so that the cube is
    // seen from the outside, which flips or swaps axes depending on the face.
    unsigned face = 0;
    float ma = 0.0;
    float sc = 0.0;
    float tc = 0.0;
    if (ax >= ay && ax >= az) {
        ma = ax;
        tc = -ry;
        if (rx >= 0.0) {
            face = 0;  // +X
            sc = -rz;
        } else {
            face = 1;  // -X
            sc = rz;
        }
    } else if (ay >= az) {
        ma = ay;
        sc = rx;
        if (ry >= 0.0) {
            face = 2;  // +Y
            tc = rz;
        } else {
            face = 3;  // -Y
            tc = -rz;
        }
    } else {
        ma = az;
        tc = -ry;
        if (rz >= 0.0) {
            face = 4;  // +Z
            sc = rx;
        } else {
            face = 5;  // -Z
            sc = -rx;
        }
    }

    if (ma == 0.0)
        return {face, 0.5, 0.5};  // a direction of no length points at nothing; the middle of a face is as good as any

    // sc/ma and tc/ma each span [-1, 1] across the face, which is remapped to the [0, 1] the face is measured in.
    return {face, 0.5f * (sc / ma + 1.0f), 0.5f * (tc / ma + 1.0f)};
}

std::tuple<unsigned, float> Image::decompose(float val) {
    // TODO this needs to be refactored since pixel centers are at 0.5. Also, bounds conditions defined by the sampler.
    float base;
    float dec = std::modf(val, &base);
    // We will be subtracting the decimal component from 1.0 later, and if the subtraction doesn't even register,
    // it is close enough to 0.0 to flatten it.
    if (1.0 - dec == 1.0)
        dec = 0.0;
    return {static_cast<unsigned>(base), dec};
}

bool Image::equals(const Value& val) const {
    if (val.getType().getBase() != type.getBase())  // guarantees matching types
        return false;
    const Image& other = static_cast<const Image&>(val);

    // reference is not compared since it only is used in generating the data
    if (xx != other.xx || yy != other.yy || zz != other.zz || layers != other.layers)
        return false;

    // The ordering of components does not have to be identical, but all active components per fragment in one image
    // need to be active in the other image too.
    if (comps.count != other.comps.count || data.size() != other.data.size())
        return false;
    for (unsigned i = 0; i < 4; ++i) {
        if ((comps[i] == 0) != (other.comps[i] == 0))
            return false;
    }

    // Do a data analysis
    // In theory, the data of all mipmaps should be synchronized. Therefore, we can compare only the mipmaps with
    // most data (mipmap 0)

    // Mipmap 0 holds comps.count entries per texel, matching the layout used by read()/write(). Each layer carries a
    // mipmap chain of its own, so the level 0 of every layer is a separate run, one layer stride from the last, rather
    // than a single block at the front of the data.
    const unsigned mip0 = texelsAt(0) * comps.count;
    const unsigned stride = layerStride();

    const Type& subelement = type.getElement();
    assert(comps.count != 0);
    for (unsigned layer = 0; layer < layers; ++layer) {
        const unsigned begin = layer * stride;
        const unsigned limit = std::min(begin + mip0, static_cast<unsigned>(data.size()));
        for (unsigned i = begin; i + comps.count <= limit; i += comps.count) {
            for (unsigned j = 0; j < 4; ++j) {
                if (comps[j] == 0)
                    continue;
                // Compare data in the primitive type (needed since float allow for a more lenient comparison)
                Primitive mine(data[i + comps[j] - 1]);
                Primitive your(other.data[i + other.comps[j] - 1]);
                mine.cast(subelement);
                your.cast(subelement);
                if (!mine.equals(your))
                    return false;
            }
        }
    }
    return true;
}

void Image::copyFrom(const Struct& str) noexcept(false) {
    const Struct& other = Statics::extractStruct(static_cast<const Value*>(&str), "image", names);

    // ref: <string>
    const Value* ref = other[0];
    if (ref->getType().getBase() != DataType::STRING)
        throw std::runtime_error("The first image field, \"ref\", must be a string path to the image source or empty!");
    reference = static_cast<const String*>(ref)->get();

    // dim: one component per spatial axis, plus a trailing array length when the image is arrayed
    const unsigned spatial = this->type.getSpatialDims();
    const bool arrayed = this->type.isArrayed();
    std::vector<uint64_t> dims = Statics::extractUvec(other[1], names[1], spatial + (arrayed ? 1 : 0));
    xx = dims[0];
    if (spatial > 1) {
        yy = dims[1];
        if (spatial > 2)
            zz = dims[2];
    }
    // The trailing component counts array elements rather than stored layers, so a cube map's six faces per element
    // are implied by the type instead of spelled out in the file.
    const unsigned array_length = arrayed ? static_cast<unsigned>(dims[spatial]) : 1;
    if (array_length == 0)
        throw std::runtime_error("The image field \"dim\" must give at least one array element for an arrayed image!");
    layers = array_length * ((this->type.getImageDim() == ImageDim::CUBE) ? 6 : 1);

    // Now that we have the expected dimensions, fetch data (if any) from the reference path
    // TODO: handle dimensions besides 2D and add support for mipmaps
    if (!reference.empty()) {
        if (layers > 1) {
            // One file holds one image, and there is no convention here yet for where the other layers would come
            // from. Refuse rather than silently filling only the first layer.
            throw std::runtime_error("Loading a layered image from a file is not yet supported!");
        }
        int width, height, channels;
        unsigned char* img = stbi_load(reference.c_str(), &width, &height, &channels, 0);
        if (img == nullptr) {
            std::stringstream err;
            err << "Could not load image from path \"" << reference << "\"!";
            throw std::runtime_error(err.str());
        }
        // I don't think the width or height should ever be negative
        assert(height >= 1 && width >= 1);

        unsigned gx = static_cast<unsigned>(width);
        unsigned gy = static_cast<unsigned>(height);
        unsigned gc = static_cast<unsigned>(channels);
        if (gx < xx || gy < yy) {
            std::stringstream err;
            err << "The dimensions of the image loaded from file (" << gx << " x " << gy << ") are insufficient";
            err << " for the image dimensions required: " << xx << " x " << yy << " x " << zz;
            throw std::runtime_error(err.str());
        }

        // The shader may or may not provide the components field.
        // - %10 = OpTypeImage %float 2D 0 0 0 2 Rgba8
        // - %11 = OpTypeImage %float 2D 0 0 0 1 Unknown
        // If it doesn't, we will default to the format of the provided image.
        if (comps.count == 0) {
            for (unsigned i = 0; i < gc; ++i)
                comps[i] = i + 1;
            comps.count = gc;
        }
        // Note, however, if it does, we need to only copy the relevant channels in the correct order from loaded data.

        // Now, transfer the data from img to our "data" field
        // Data has been loaded in as a sequence of RGBA bytes (values 0-255) from left -> right, top -> bottom.
        // TODO handle more than just unsigned normalized float element type
        data.resize(gx * gy * comps.count);
        unsigned load_idx = 0;
        unsigned store_idx = 0;
        for (unsigned y = 0; y < gy; ++y) {
            for (unsigned x = 0; x < gx; ++x) {
                for (unsigned c = 0; c < gc; ++c) {
                    if (comps[c] == 0)
                        continue;
                    double norm = img[load_idx + c] / 255.0;
                    data[store_idx + comps[c] - 1] = std::bit_cast<uint64_t>(norm);
                }
                load_idx += gc;
                store_idx += comps.count;
            }
        }

        // Finally, delete the image loaded
        stbi_image_free(img);
        // example write: stbi_write_png("sky.png", width, height, channels, img, width * channels);
    }

    // mipmaps: <uint>
    mipmaps = Statics::extractUint(other[2], names[2]);
    if (mipmaps == 0)
        throw std::runtime_error("The image field \"mipmaps\" is an must have an integer value greater than 0!");
    // A chain halves the largest axis each level, bottoming out at 1 after floor(log2(maxdim)) + 1 levels, which is
    // what std::bit_width returns. More than that would name levels no image this size can have, and would break
    // mipDivisor's precondition that a level index fits the shift, so reject it here rather than accept a bad count.
    const unsigned max_extent = std::max({xx, yy, zz, 1u});
    const unsigned max_mipmaps = std::bit_width(max_extent);
    if (mipmaps > max_mipmaps) {
        std::stringstream err;
        err << "The image field \"mipmaps\" is " << mipmaps << ", but an image whose largest dimension is "
            << max_extent << " can have at most " << max_mipmaps << " mipmap level(s)!";
        throw std::runtime_error(err.str());
    }

    // comps: <uint>
    const Value& comps_v = *other[3];
    if (comps_v.getType().getBase() != DataType::UINT)
        throw std::runtime_error(
            "The fourth image field, \"comps\", must be an unsigned int specifying the presence and order of the "
            "pixel components: Red, Green, Blue, Alpha (in that order). For example: \"1234\" indicates all four "
            "channels are present in their default order; \"0010\" indicates only blue is present; \"2341\" means "
            "that all four channels are present in the order ARGB."
        );
    unsigned comps_got = static_cast<const Primitive&>(comps_v).data.all;
    Component comps_new(comps_got, reference.empty());  // only used if no reference is given
    if (reference.empty())
        comps.assertCompatible(comps_new);

    // data : array<float> or array<uint> or array<int>
    // TODO: differentiate between float [0, 255] and float normal [0.0, 1.0]
    const Value& data_v = *other[4];
    if (data_v.getType().getBase() != DataType::ARRAY)
        throw std::runtime_error("The fourth image field, \"data\", must be an array of uint, int, or float values.");
    const Array& data_a = static_cast<const Array&>(data_v);
    if (!reference.empty()) {
        // Verify that the data is empty
        if (data_a.getSize() != 0) {
            throw std::runtime_error(
                "Image exists with both an image reference and literal data. Only one may be provided at a time!"
            );
        }

        // Clear out the reference because when the output prints, we cannot print reference and data
        reference = "";
        fromFile = true;
    } else {
        const Type& element = data_a.getType().getElement();
        if (DataType ebase = element.getBase();
            ebase != DataType::FLOAT && ebase != DataType::UINT && ebase != DataType::INT)
            throw std::runtime_error("The image field \"data\" must have elements of type: uint, int, or float!");
        unsigned size = data_a.getSize();
        // Verify that the data matches expected from the given dimensions
        unsigned total = layers * layerStride();
        if (total != size) {
            std::stringstream err;
            err << "The amount of data provided for the image does not match the dimensions given! Dimensions were ";
            err << xx << " x " << yy << " x " << zz << " over " << layers << " layer(s), with " << comps.count;
            err << " active channels. This requires ";
            err << total << " values, however, " << size << " were provided.";
            throw std::runtime_error(err.str());
        }
        // Now copy the data over
        data.resize(size);
        // TODO actually cannot do this in case the data elements have different type :/
        for (unsigned i = 0; i < size; i += comps.count) {
            for (unsigned j = 0; j < 4; ++j) {
                if (comps[j] == 0)
                    continue;
                const auto& prim = static_cast<const Primitive&>(*data_a[i + comps_new[j] - 1]);
                data[i + comps[j] - 1] = prim.data.all;
            }
        }
    }
}

void Image::copyFrom(const Value& new_val) noexcept(false) {
    // Can copy from a struct, assuming that the correct fields are present
    if (const auto& new_type = new_val.getType(); new_type.getBase() == DataType::STRUCT) {
        copyFrom(static_cast<const Struct&>(new_val));
        return;  // will either throw an exception or do a successful copy
    }

    Value::copyFrom(new_val);  // verifies matching types
    const Image& other = static_cast<const Image&>(new_val);
    comps.assertCompatible(other.comps);

    this->xx = other.xx;
    this->yy = other.yy;
    this->zz = other.zz;
    this->layers = other.layers;
    this->mipmaps = other.mipmaps;

    // Now, copy over the data:
    // If a string reference is defined in the other, load data from file TODO
    // Otherwise, do a copy of the other's data array
    data.resize(other.data.size());
    // TODO actually cannot do this in case the data elements may have different type :/
    for (unsigned i = 0; i < data.size(); i += comps.count) {
        for (unsigned j = 0; j < 4; ++j) {
            if (comps[j] == 0)
                continue;
            data[i + comps[j] - 1] = other.data[i + other.comps[j] - 1];
        }
    }
}

Struct* Image::toStruct() const {
    std::vector<Value*> elements;
    elements.reserve(names.size());
    elements.push_back(new String(reference));
    std::vector<Value*> dims;
    const unsigned spatial = type.getSpatialDims();
    const bool arrayed = type.isArrayed();
    dims.reserve(spatial + (arrayed ? 1 : 0));
    dims.push_back(new Primitive(xx));
    if (spatial > 1) {
        dims.push_back(new Primitive(yy));
        if (spatial > 2)
            dims.push_back(new Primitive(zz));
    }
    // Mirror what copyFrom reads: the array length, with a cube map's faces left implicit.
    if (arrayed)
        dims.push_back(new Primitive(arrayLength()));
    elements.push_back(new Array(dims));
    elements.push_back(new Primitive(mipmaps));
    // Reconstruct the components uint from the actual components breakdown
    elements.push_back(new Primitive(comps.r * 1000 + comps.g * 100 + comps.b * 10 + comps.a));
    Array* dat = new Array(type.getElement(), 0);
    // populate the dat array with the image's actual data
    std::vector<Value*> values;
    const Type& dat_type = type.getElement();
    for (const uint64_t dat : data) {
        Primitive* prim = new Primitive(dat);
        prim->cast(dat_type);
        values.push_back(prim);
    }
    dat->setElementsDirectly(values);
    elements.push_back(dat);
    return new Struct(elements, names);
}

Image::Location Image::extractCoords(const Value* coords_v, const Type& img_type, Access access, bool proj) {
    const Type* coord_type = &coords_v->getType();
    bool aggregate = false;
    if (coord_type->getBase() == DataType::ARRAY) {
        coord_type = &coord_type->getElement();
        aggregate = true;
    }
    const DataType base = coord_type->getBase();

    auto get = [](const Value* val, DataType base) {
        const auto& prim = static_cast<const Primitive&>(*val);
        if (base == DataType::INT)
            return static_cast<double>(prim.data.i);
        if (base == DataType::UINT)
            return static_cast<double>(prim.data.u);
        assert(base == DataType::FLOAT);
        return prim.data.f;
    };

    // The components come in a fixed order: every spatial one, then the layer, then the projective divisor. Only the
    // spatial ones are always present.
    const bool arrayed = img_type.isArrayed();
    // A cube map is the one dimensionality whose coordinate depends on how the image is reached. Sampling supplies a
    // direction vector from the center of the cube, so all three components are spatial and the face is derived from
    // them. Addressing it directly instead names the face, leaving a flat position behind, which is the same shape any
    // other layered image takes. So a direct cube access always has a layer component, arrayed or not, and that layer
    // already counts faces.
    const bool direct_cube = (img_type.getImageDim() == ImageDim::CUBE) && access == Access::DIRECT;
    const unsigned spatial = direct_cube ? img_type.getSpatialDims() : (img_type.getCoordCount() - (arrayed ? 1 : 0));
    const bool has_layer = arrayed || direct_cube;

    Location loc;
    loc.access = access;
    if (!aggregate) {
        // A lone spatial axis with no layer and no divisor is the only case that fits in a scalar
        assert(spatial == 1 && !has_layer && !proj);
        loc.x = get(coords_v, base);
        return loc;
    }

    const auto& coords = static_cast<const Array&>(*coords_v);
    assert(coords.getSize() >= spatial + (has_layer ? 1u : 0u) + (proj ? 1u : 0u));
    loc.x = get(coords[0], base);
    if (spatial >= 2) {
        loc.y = get(coords[1], base);
        if (spatial >= 3)
            loc.z = get(coords[2], base);
    }
    unsigned next = spatial;
    if (has_layer)
        loc.layer = get(coords[next++], base);
    if (proj)
        loc.q = get(coords[next], base);
    return loc;
}

[[nodiscard]] Array* Image::read(const Location& loc) const {
    float x = loc.x;
    float y = loc.y;
    float z = loc.z;
    const float lod = loc.lod;

    // The layer is selected, not interpolated, so it only decides where this read begins.
    unsigned layer;
    if (loc.access == Access::DIRECT) {
        // A direct access names a whole layer, faces and all. Naming one that does not exist is out of bounds, the
        // same as any other coordinate past its extent, rather than something to clamp.
        const float rounded = std::nearbyint(loc.layer);
        if (rounded < 0.0 || rounded >= static_cast<float>(layers))
            return outOfBoundsAccess();
        layer = static_cast<unsigned>(rounded);
    } else {
        // A sampled access gives an array element, which layerIndex clamps as a sampled access is specified to.
        layer = layerIndex(loc.layer);
        if (type.getImageDim() == ImageDim::CUBE) {
            // The coordinate is a direction from the center of the cube rather than a position on a face, so it has to
            // pick a face first. Faces are layers, consecutive within an array element, so the face joins the layer
            // instead of becoming a third coordinate.
            const CubeFace at = selectCubeFace(x, y, z);
            layer += at.face;
            // TODO The face position is mapped onto the interpreter's coordinates, which run from the center of the
            // first texel to the center of the last. Vulkan instead puts s == 0 half a texel outside the first center,
            // which is what lets filtering at a face's edge reach the neighboring face. Adopting that needs those
            // cross-face fetches to exist first, so until then the outermost half texel is not blended across a seam.
            x = at.s * static_cast<float>(std::max(xx, 1u) - 1);
            y = at.t * static_cast<float>(std::max(yy, 1u) - 1);
            z = 0.0;
        }
    }

    if (x < 0 || y < 0 || z < 0 || lod < 0)
        return outOfBoundsAccess();

    const unsigned layer_base = layer * layerStride();

    // coordinates are given in the scale of lod=0, regardless of the actual lod to use
    auto [lBase, lRatio] = decompose(lod);
    {  // put this test in its own scope because we don't want the decomposed (besides lod) leaking out accidentally
        auto [xBase, xRatio] = decompose(x);
        auto [yBase, yRatio] = decompose(y);
        auto [zBase, zRatio] = decompose(z);

        if ((xBase > xx || (xBase == xx && xRatio > 0.0)) || (yBase > yy || (yBase == yy && yRatio > 0.0)) ||
            (zBase > zz || (zBase == zz && zRatio > 0.0)) || (lBase > mipmaps || (lBase == mipmaps && lRatio > 0.0)))
            return outOfBoundsAccess();
    }

    auto for_lod = [](float coord, unsigned size, unsigned lod) {
        if (coord == 0.0)
            return std::tuple<unsigned, float>(0, 0.0);
        if (lod == 0)
            return decompose(coord);

        // we divide each dimension by 2 times the lod. For example, 0 is full size, 1 is half-size, etc
        unsigned divide = mipDivisor(lod);
        unsigned trunc = std::max(size / divide, 1u);
        // The integral division truncates, which means the actual divisor may exceed divide
        float actual_div = float(size) / float(trunc);
        float actual_rat = float(trunc) / float(size);

        // If the coord was between pixels which got consolidated, any decimal part it had should be erased.
        // Consider this example:
        // - coord 0.2 is 1/5 of the way between 0 and 1. In the mipmap, 0-1 is represented fully by the new 0. We
        // should not get *any* blending with the new 1, which represents the top-level 2-3.
        // However, if the coord was between pixels of different groups, the decimal part should be undisturbed.
        // Consider another example:
        // - coord 1.75 is 3/4 the way between 1 and 2. In the mipmap, 0-1 is represented by the new 0, and 2-3 is
        // represented by the new 1. This scaling did *not* affect the ratio of the original coord's representation
        // by the now pixel 0 and 1.

        // This is complicated by the fact that pixel boundaries are not even if actual_div != divide.
        // Compute the coordinate with the correct scale
        float offset = std::fmod(coord, actual_div);
        float pix_size = actual_div / float(divide);
        float dec = 0.0;
        if (offset > actual_div - pix_size) {
            dec = 1.0 - (actual_div - offset) / pix_size;
            if (1.0 - dec == 1.0)
                dec = 0.0;
        }

        float lowered = coord * actual_rat;
        unsigned integral = static_cast<unsigned>(std::floor(lowered));
        return std::tuple<unsigned, float>(integral, dec);
    };

    const Type& el = type.getElement();
    const DataType el_base = el.getBase();

    // Perform interpolation for all affected values. A single texel cannot have more than 4 components.
    float sums[] = {0.0, 0.0, 0.0, 0.0};
    for (unsigned which_lod = 0; which_lod < 2; ++which_lod) {
        unsigned use_lod = lBase + which_lod;
        float lod_weight = (which_lod == 0) ? (1.0 - lRatio) : lRatio;
        if (lod_weight == 0.0)
            break;

        std::vector<std::tuple<unsigned, float>> interps;
        // Recompute the base and ratio for the given level of detail
        auto [bx, rx] = for_lod(x, xx, use_lod);
        auto [by, ry] = for_lod(y, yy, use_lod);
        auto [bz, rz] = for_lod(z, zz, use_lod);

        // Determine the "anchor", which is the data index which points to (bx, by, bz) for this lod.
        // We add some factor to the anchor to calculate the location of the alternate texel (ie, `b + 1`), for each
        // coordinate with nonzero ratio.
        const unsigned div = mipDivisor(use_lod);
        const unsigned xxx = std::max(xx / div, 1u);
        const unsigned yyy = std::max(yy / div, 1u);
        unsigned anchor = layer_base + lodOffset(use_lod);

        unsigned factor = comps.count;
        if (rx > 0.0)
            interps.push_back({factor, rx});
        anchor += bx * factor;
        factor *= xxx;
        if (ry > 0.0)
            interps.push_back({factor, ry});
        anchor += by * factor;
        factor *= yyy;
        if (rz > 0.0)
            interps.push_back({factor, rz});
        anchor += bz * factor;

        // We need every combo of different interps applied (either off or on), which maps perfectly onto bits
        // counting to 2^n, where n is the maximum number of interps.
        // Each bit in the increment variable corresponds to whether that interpolation index should be on
        for (size_t i = 0; i < (1u << interps.size()); ++i) {
            unsigned total = anchor;
            float weight = lod_weight;
            for (unsigned bit = 0; bit < interps.size(); ++bit) {
                auto [delta, this_ratio] = interps[bit];
                if ((i >> bit) & 0x1) {
                    total += delta;
                    weight *= this_ratio;
                } else {
                    weight *= (1.0 - this_ratio);
                }
            }
            // Now that we have determined the location and the total weight, add to sum
            for (unsigned chan = 0; chan < comps.count; ++chan) {
                assert(total + chan < data.size());  // safety assert for what should already have been checked
                Primitive prim(data[total + chan]);
                float converted;
                if (el_base == DataType::FLOAT) {
                    converted = prim.data.f;
                } else if (el_base == DataType::INT) {
                    converted = prim.data.i;
                } else {
                    assert(el_base == DataType::UINT);
                    converted = prim.data.u;
                }
                sums[chan] += (converted * weight);
            }
        }
    }

    // The size of the array returned is the number of components in each texel
    std::vector<Value*> vals(comps.count, nullptr);

    // SPIR-V returns a texel as an RGBA-ordered vector and #data is stored that way, so the channels come back in
    // storage order directly.
    assert(comps.isAscending());
    for (unsigned chan = 0; chan < comps.count; ++chan) {
        float sum = sums[chan];
        Primitive from(0u);
        if (el_base == DataType::FLOAT) {
            from = Primitive(sum);
        } else if (el_base == DataType::INT) {
            from = Primitive(static_cast<int64_t>(sum));
        } else {
            assert(el_base == DataType::UINT);
            from = Primitive(static_cast<unsigned>(sum));
        }
        auto* prim = new Primitive(0u);
        prim->cast(el);
        prim->copyFrom(from);
        vals[chan] = prim;
    }

    return new Array(vals);
}

[[nodiscard]] Array* Image::expandToRgba(const Array& texel) const {
    assert(texel.getSize() == comps.count);
    assert(comps.isAscending());
    const Type& el = type.getElement();
    const DataType el_base = el.getBase();

    // The fill has to be of the texel type, the same way read() builds its results.
    auto typed = [el_base](double val) {
        if (el_base == DataType::FLOAT)
            return Primitive(val);
        if (el_base == DataType::INT)
            return Primitive(static_cast<int64_t>(val));
        assert(el_base == DataType::UINT);
        return Primitive(static_cast<uint64_t>(val));
    };

    std::vector<Value*> vals(4, nullptr);
    for (unsigned chan = 0; chan < 4; ++chan) {
        auto* prim = new Primitive(0u);
        prim->cast(el);
        if (comps[chan] == 0) {
            // Absent channels read as zero, except alpha, which reads as one so that a color without one is opaque.
            // This is the substitution Vulkan mandates, in the format's numeric type (0.0/1.0 for float and
            // normalized formats, integer 0/1 otherwise); see "Component Substitution":
            // https://docs.vulkan.org/spec/latest/chapters/resources.html#images-component-substitution
            prim->copyFrom(typed((chan == 3) ? 1.0 : 0.0));
        } else {
            // comps gives the slot this channel occupies, which is where read() left it.
            prim->copyFrom(*texel[comps[chan] - 1]);
        }
        vals[chan] = prim;
    }
    return new Array(vals);
}

std::array<unsigned, 4> Image::getSize(uint32_t lod) const {
    // Each spatial axis halves per mipmap level. For example, 0 is full size, 1 is half-size, etc
    const unsigned divide = mipDivisor(lod);
    auto trunc = [divide](unsigned size) { return std::max(size / divide, 1u); };
    const unsigned spatial = type.getSpatialDims();
    assert(spatial < 4);

    std::array<unsigned, 4> size {0, 0, 0, 0};
    size[0] = trunc(xx);
    if (spatial > 1) {
        size[1] = trunc(yy);
        if (spatial > 2)
            size[2] = trunc(zz);
    }
    // The array length follows the spatial axes with no gap, and is reported undivided: a mipmap level subdivides the
    // axes and leaves the layers alone, so textureSize on a cube array counts cubes rather than faces.
    if (type.isArrayed())
        size[spatial] = arrayLength();
    return size;
}

bool Image::write(int x, int y, int z, int layer, const Array& texel) {
    // Verify that the texel to write to is in bounds
    bool oob = (x < 0 || y < 0 || z < 0 || layer < 0);

    unsigned xu = static_cast<unsigned>(x);
    unsigned yu = static_cast<unsigned>(y);
    unsigned zu = static_cast<unsigned>(z);
    unsigned lu = static_cast<unsigned>(layer);
    // If the coordinate specified matches or exceeds the maximum (exclusive), then we are out of bounds.
    // However, there is some special behavior for 0, since coordinate matching is appropriate there.
    if ((xu > 0 && xu >= xx) || (yu > 0 && yu >= yy) || (zu > 0 && zu >= zz))
        oob = true;
    // The layer of a write is a whole index into the stored layers, so unlike the clamp a sampled read performs,
    // naming a layer that does not exist is out of bounds like any other coordinate.
    if (lu >= layers)
        oob = true;
    if (oob)
        return false;

    unsigned yyy = xx * comps.count;
    unsigned zzz = yy * yyy;
    unsigned base = (lu * layerStride()) + (xu * comps.count) + (yu * yyy) + (zu * zzz);
    assert(base < data.size());  // should be checked in copying that dimensions match data count actually given

    // TODO: write at the same location to all mipmaps

    // OpImageWrite supplies an RGBA-ordered vector, matching how #data is stored, so its channels drop in by position
    assert(comps.isAscending());
    const Type& el = type.getElement();
    assert(el.isPrimitive());
    Primitive dummy(el, false);
    unsigned tex_size = texel.getSize();
    assert(tex_size <= 4);  // texel array to write cannot have more than 4 channels!
    tex_size = std::min(tex_size, comps.count);
    assert(base + tex_size <= data.size());
    for (unsigned i = 0; i < tex_size; ++i) {
        dummy.copyFrom(*texel[i]);
        auto got = dummy.data.all;
        data[base + i] = got;
    }

    return true;
}
