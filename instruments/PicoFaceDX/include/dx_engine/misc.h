#pragma once
#include <cstdint>
#include <cmath>
#include "ram_hot.h"

inline __attribute__((always_inline)) float RAM_HOT(fclamp)(float smp, float a, float b) {
   if (smp>b) return b;
   if (smp>=a) return smp;
   return a;
}
inline __attribute__((always_inline)) float RAM_HOT(midiNoteToHz)(uint8_t note) {
    return 440.0f * powf(2.0f, (int(note) - 69) / 12.0f);
}
inline __attribute__((always_inline)) float RAM_HOT(saturate_cubic)(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return 1.5f * (x - (x * x * x) * 0.3334f);
}
inline float __attribute__((always_inline)) RAM_HOT(fast_floorf)(float x) {
    int i = (int)x;
    return (float)(i - (int)(i > x));
}
inline float __attribute__((always_inline)) RAM_HOT(wrap01)(float x) { return x - fast_floorf(x); }

inline uint32_t lfsr_ = 0x12345678;
inline float randomFloat() {
    lfsr_ ^= lfsr_ << 13; lfsr_ ^= lfsr_ >> 17; lfsr_ ^= lfsr_ << 5;
    return ((lfsr_ & 0xFFFFFF) / float(0x800000) - 1.f);
}
