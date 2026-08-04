#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline float fastTanh(float x) {
    x = x > 3.0f ? 3.0f : (x < -3.0f ? -3.0f : x);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float fastTan(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return x * (945.0f - 105.0f * x2 + x4) / (945.0f - 420.0f * x2 + 15.0f * x4);
}
