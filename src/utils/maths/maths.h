#pragma once
#include <cstdint>

inline int32_t floor_div(int32_t a, int32_t b) {
    const int32_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

inline std::int32_t floor_mod(int32_t a, int32_t b) {
    const int32_t r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}