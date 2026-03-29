#pragma once

inline int floor_div(int a, int b) {
    return a / b - (a % b != 0 && (a ^ b) < 0);
}

inline int pos_mod(int a, int b) {
    return ((a % b) + b) % b;
}