/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <vector>

#include "glm/mat4x3.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#include "../type.hpp"
#include "../value.hpp"
export module value.raytrace.rayQuery;
import value.raytrace.accelStruct;
import value.aggregate;
import value.primitive;
import value.string;

export class RayQuery final : public Value {
private:
    AccelStruct accelStruct;

    static std::vector<Primitive> fromMat4x3(glm::mat4x3& mat) {
        std::vector<Primitive> ret;
        ret.reserve(3 * 4);
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned j = 0; j < 4; ++j)
                ret.emplace_back(mat[j][i]);
        }
        return ret;
    }

public:
    RayQuery() : Value(Type::rayQuery()) {}

    void setAccelStruct(AccelStruct& as) {
        accelStruct = as;
    }

    AccelStruct& getAccelStruct() {
        return accelStruct;
    }

    /// @brief Get the current intersection's barycentric coordinates.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return barycentrics.
    std::vector<Primitive> getIntersectionBarycentrics(bool get_committed) const {
        glm::vec2 bary = accelStruct.getIntersectionBarycentrics(get_committed);
        return std::vector<Primitive> {Primitive(bary.x), Primitive(bary.y)};
    }

    /// @brief Get the object-space ray direction depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray direction.
    std::vector<Primitive> getIntersectionObjectRayDirection(bool get_committed) const {
        glm::vec3 ray_dir = accelStruct.getIntersectionObjectRayDirection(get_committed);
        return std::vector<Primitive> {ray_dir.x, ray_dir.y, ray_dir.z};
    }

    /// @brief Get the object-space ray origin depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray origin.
    std::vector<Primitive> getIntersectionObjectRayOrigin(bool get_committed) const {
        glm::vec3 ray_pos = accelStruct.getIntersectionObjectRayOrigin(get_committed);
        return std::vector<Primitive> {ray_pos.x, ray_pos.y, ray_pos.z};
    }

    /// @brief Get the object-to-world matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-to-world matrix.
    std::vector<Primitive> getIntersectionObjectToWorld(bool get_committed) const {
        glm::mat4x3 got = accelStruct.getIntersectionObjectToWorld(get_committed);
        return fromMat4x3(got);
    }

    /// @brief Get the world-to-object matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return world-to-object matrix.
    std::vector<Primitive> getIntersectionWorldToObject(bool get_committed) const {
        glm::mat4x3 got = accelStruct.getIntersectionWorldToObject(get_committed);
        return fromMat4x3(got);
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<std::string> names {"accel-struct"};
        std::vector<Value*> fields {accelStruct.toStruct()};
        return new Struct(fields, names);
    }

    void copyFrom(const Value& new_value) noexcept(false) override {
        throw std::runtime_error("Unimplemented copy from ray query!");
    }

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to ray query!");
    }
};
