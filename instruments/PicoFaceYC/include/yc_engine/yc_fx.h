

// include/yc_engine/yc_fx.h
#pragma once
#include "yc_core.h"
#include "yc_lut_data.h"
#include <cmath>

static inline float yc_overdrive_process(const yc_engine_state_t& state, float dry_sample)
{
    if (state.distortion == 0)
        return dry_sample;

    float pre_gain = 1.0f + (state.distortion / 127.0f) * 9.0f;
    float x = dry_sample * pre_gain;
    if (x < -1.0f) x = -1.0f;
    if (x >  1.0f) x =  1.0f;

    float idx_f = (x + 1.0f) * 0.5f * 1023.0f;
    int idx0 = (int)idx_f;
    if (idx0 > 1022) idx0 = 1022;
    int idx1 = idx0 + 1;
    if (idx1 > 1023) idx1 = 1023;
    float frac = idx_f - idx0;

    return yc_overdrive_lut[idx0] + frac * (yc_overdrive_lut[idx1] - yc_overdrive_lut[idx0]);
}

// Soft-Clip ueber LUT (yc_softclip_lut = tanh auf t in [0, 8]) statt tanhf():
// tanhf ist ein teurer libm-Aufruf mitten im Audio-Pfad (RP2350: Software-
// Float). Kurve und Schwellwert 0.9 bleiben identisch (max. Abweichung durch
// die Interpolation < 1e-5). Ausgabe ist garantiert auf [-1, 1] begrenzt.
inline float yc_soft_clip(float x) {
    float ax = fabsf(x);
    if (ax < 0.9f) return x;
    float t = (ax - 0.9f) / 0.1f;
    // !(t < 8) statt (t >= 8): faengt auch NaN-Eingaben sicher (als +/-1) ab,
    // statt ausserhalb der Tabelle zu lesen. tanh(8) ist in float32 exakt 1.
    if (!(t < 8.0f)) {
        return (x >= 0.0f) ? 1.0f : -1.0f;
    }
    float idx_f = t * (1023.0f / 8.0f);
    int idx0 = (int)idx_f;              // t < 8  ->  idx0 <= 1022
    int idx1 = idx0 + 1;                //                idx1 <= 1023
    float frac = idx_f - (float)idx0;
    float th = yc_softclip_lut[idx0] + (yc_softclip_lut[idx1] - yc_softclip_lut[idx0]) * frac;
    float y = 0.9f + 0.1f * th;
    return (x >= 0.0f) ? y : -y;
}

