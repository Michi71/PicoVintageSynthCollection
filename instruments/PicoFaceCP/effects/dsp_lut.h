#pragma once
// dsp_lut.h — Shared sine lookup table (512 entries).
// Initialised lazily by g_sinLUT(). Used by all LFOs in the effect chain
// to replace per-sample sinf() calls (~50-100 cycles → ~5-10 cycles on M33).

#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <int N>
class SinLUT {
  static_assert((N & (N - 1)) == 0 && N > 0, "N must be a power of two");
  float table[N];

  static constexpr float kTwoPi = 2.0f * (float)M_PI;
  static constexpr float kInvN  = 1.0f / (float)N;

public:
  void init() {
    for (int i = 0; i < N; ++i)
      table[i] = sinf(kTwoPi * (float)i * kInvN);
  }

  float sin01(float phase) {
    float p = phase - floorf(phase);
    float s = p * (float)N;
    int   idx  = (int)s;
    float frac = s - (float)idx;
    idx &= (N - 1);
    float a = table[idx];
    float b = table[(idx + 1) & (N - 1)];
    return a + (b - a) * frac;
  }

  float sin01_nofloor(float phase) {
    float s = phase * (float)N;
    int   idx  = (int)s;
    float frac = s - (float)idx;
    idx &= (N - 1);
    float a = table[idx];
    float b = table[(idx + 1) & (N - 1)];
    return a + (b - a) * frac;
  }
};

// Global shared sine LUT (512 entries). First call lazily initialises it.
inline SinLUT<512>& g_sinLUT() {
  static SinLUT<512> lut;
  static bool inited = false;
  if (!inited) { lut.init(); inited = true; }
  return lut;
}
