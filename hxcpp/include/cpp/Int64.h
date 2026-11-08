#pragma once

#include <hxcpp.h>

#include <limits>
#include <stdexcept>

inline ::cpp::Int64 _hx_int64_make(int high, int low) noexcept {
    return static_cast<::cpp::Int64>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32U) |
        static_cast<std::uint32_t>(low));
}

inline int _hx_int64_low(::cpp::Int64 value) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(value));
}

inline int _hx_int64_high(::cpp::Int64 value) noexcept {
    return static_cast<int>(static_cast<std::uint64_t>(value) >> 32U);
}

inline ::cpp::Int64 _hx_int64_add(::cpp::Int64 left,
                                  ::cpp::Int64 right) noexcept {
    return static_cast<::cpp::Int64>(static_cast<std::uint64_t>(left) +
                                     static_cast<std::uint64_t>(right));
}
inline ::cpp::Int64 _hx_int64_sub(::cpp::Int64 left,
                                  ::cpp::Int64 right) noexcept {
    return static_cast<::cpp::Int64>(static_cast<std::uint64_t>(left) -
                                     static_cast<std::uint64_t>(right));
}
inline ::cpp::Int64 _hx_int64_mul(::cpp::Int64 left,
                                  ::cpp::Int64 right) noexcept {
    return static_cast<::cpp::Int64>(static_cast<std::uint64_t>(left) *
                                     static_cast<std::uint64_t>(right));
}
inline ::cpp::Int64 _hx_int64_neg(::cpp::Int64 value) noexcept {
    return static_cast<::cpp::Int64>(std::uint64_t{0} -
                                     static_cast<std::uint64_t>(value));
}
inline ::cpp::Int64 _hx_int64_complement(::cpp::Int64 value) noexcept {
    return static_cast<::cpp::Int64>(~static_cast<std::uint64_t>(value));
}

inline ::cpp::Int64 _hx_int64_div(::cpp::Int64 left, ::cpp::Int64 right) {
    if (right == 0)
        throw std::domain_error("Int64 division by zero");
    if (left == std::numeric_limits<::cpp::Int64>::min() && right == -1)
        return left;
    return left / right;
}
inline ::cpp::Int64 _hx_int64_mod(::cpp::Int64 left, ::cpp::Int64 right) {
    if (right == 0)
        throw std::domain_error("Int64 modulo by zero");
    if (left == std::numeric_limits<::cpp::Int64>::min() && right == -1)
        return 0;
    return left % right;
}

inline ::cpp::Int64 _hx_int64_and(::cpp::Int64 left,
                                  ::cpp::Int64 right) noexcept {
    return left & right;
}
inline ::cpp::Int64 _hx_int64_or(::cpp::Int64 left,
                                 ::cpp::Int64 right) noexcept {
    return left | right;
}
inline ::cpp::Int64 _hx_int64_xor(::cpp::Int64 left,
                                  ::cpp::Int64 right) noexcept {
    return left ^ right;
}
inline ::cpp::Int64 _hx_int64_shl(::cpp::Int64 value, int bits) noexcept {
    return static_cast<::cpp::Int64>(static_cast<std::uint64_t>(value) <<
                                     (static_cast<unsigned>(bits) & 63U));
}
inline ::cpp::Int64 _hx_int64_shr(::cpp::Int64 value, int bits) noexcept {
    return value >> (static_cast<unsigned>(bits) & 63U);
}
inline ::cpp::Int64 _hx_int64_ushr(::cpp::Int64 value, int bits) noexcept {
    return static_cast<::cpp::Int64>(static_cast<std::uint64_t>(value) >>
                                     (static_cast<unsigned>(bits) & 63U));
}

inline bool _hx_int64_eq(::cpp::Int64 left, ::cpp::Int64 right) noexcept {
    return left == right;
}
inline bool _hx_int64_neq(::cpp::Int64 left, ::cpp::Int64 right) noexcept {
    return left != right;
}
inline bool _hx_int64_is_zero(::cpp::Int64 value) noexcept { return value == 0; }
inline bool _hx_int64_is_neg(::cpp::Int64 value) noexcept { return value < 0; }
inline int _hx_int64_compare(::cpp::Int64 left, ::cpp::Int64 right) noexcept {
    return (left > right) - (left < right);
}
inline int _hx_int64_ucompare(::cpp::Int64 left, ::cpp::Int64 right) noexcept {
    const auto unsignedLeft = static_cast<std::uint64_t>(left);
    const auto unsignedRight = static_cast<std::uint64_t>(right);
    return (unsignedLeft > unsignedRight) - (unsignedLeft < unsignedRight);
}

inline ::cpp::Int64 _hx_int64_pre_increment(::cpp::Int64& value) noexcept {
    value = _hx_int64_add(value, 1);
    return value;
}
inline ::cpp::Int64 _hx_int64_post_increment(::cpp::Int64& value) noexcept {
    const ::cpp::Int64 previous = value;
    value = _hx_int64_add(value, 1);
    return previous;
}
inline ::cpp::Int64 _hx_int64_pre_decrement(::cpp::Int64& value) noexcept {
    value = _hx_int64_sub(value, 1);
    return value;
}
inline ::cpp::Int64 _hx_int64_post_decrement(::cpp::Int64& value) noexcept {
    const ::cpp::Int64 previous = value;
    value = _hx_int64_sub(value, 1);
    return previous;
}
