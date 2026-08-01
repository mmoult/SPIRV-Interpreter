/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_BITS_HPP
#define UTIL_BITS_HPP

#include <concepts>
#include <cstdint>
#include <limits>

/// Helpers for shifts and masks whose width may come from shader data.
///
/// C++ leaves a shift undefined when the count is negative or is greater than or equal to the width of the promoted
/// left operand. SPIR-V, by contrast, leaves only the *result* of an over-shift undefined, which permits any value but
/// does not license undefined behavior in the interpreter itself. These helpers pick zero for that case so that a
/// hostile or malformed module cannot make the interpreter's behavior undefined.
namespace Bits {

/// @brief Shift left, yielding 0 rather than undefined behavior when the count is too large.
template<std::unsigned_integral T>
[[nodiscard]] constexpr T safeShl(T val, uint64_t count) noexcept {
    if (count >= static_cast<uint64_t>(std::numeric_limits<T>::digits))
        return T(0);
    return static_cast<T>(val << count);
}

/// @brief Shift right, yielding 0 rather than undefined behavior when the count is too large.
template<std::unsigned_integral T>
[[nodiscard]] constexpr T safeShr(T val, uint64_t count) noexcept {
    if (count >= static_cast<uint64_t>(std::numeric_limits<T>::digits))
        return T(0);
    return static_cast<T>(val >> count);
}

/// @brief A value whose low `count` bits are set to one. Correct for count == 0 and for count == the width of T,
///        both of which the idiomatic `(T(1) << count) - 1` gets wrong (the latter shifts as wide as the type).
template<std::unsigned_integral T>
[[nodiscard]] constexpr T ones(unsigned count) noexcept {
    if (count >= static_cast<unsigned>(std::numeric_limits<T>::digits))
        return static_cast<T>(~T(0));
    return static_cast<T>((T(1) << count) - 1);
}

static_assert(ones<uint64_t>(0) == 0);
static_assert(ones<uint64_t>(1) == 0x1);
static_assert(ones<uint64_t>(32) == 0xFFFF'FFFF);
static_assert(ones<uint64_t>(63) == 0x7FFF'FFFF'FFFF'FFFF);
static_assert(ones<uint64_t>(64) == 0xFFFF'FFFF'FFFF'FFFF);
static_assert(ones<uint32_t>(32) == 0xFFFF'FFFF);
static_assert(safeShl<uint32_t>(1, 31) == 0x8000'0000);
static_assert(safeShl<uint32_t>(1, 32) == 0);
static_assert(safeShr<uint32_t>(0xFFFF'FFFF, 32) == 0);
static_assert(safeShl<uint64_t>(1, 63) == 0x8000'0000'0000'0000);
static_assert(safeShl<uint64_t>(1, 64) == 0);

};  // namespace Bits
#endif
