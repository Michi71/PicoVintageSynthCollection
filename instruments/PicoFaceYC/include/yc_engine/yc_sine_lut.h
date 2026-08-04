

#pragma once
#include "yc_core.h"
#include "yc_lut_data.h"
#include <cmath>

constexpr int YC_SINE_LUT_SIZE = 1024;

// phase_rad muss in [0, 2*PI) liegen (Aufrufer wrappen bereits per fmodf/while-Subtract).
inline float yc_sinf_lut(float phase_rad) {
    float idx_f = phase_rad * ((float)YC_SINE_LUT_SIZE / YC_2PI);
    int idx0 = (int)idx_f;
    int idx1 = idx0 + 1;
    if (idx1 >= YC_SINE_LUT_SIZE) idx1 = 0;
    float frac = idx_f - (float)idx0;
    return yc_sine_lut[idx0] + (yc_sine_lut[idx1] - yc_sine_lut[idx0]) * frac;
}

