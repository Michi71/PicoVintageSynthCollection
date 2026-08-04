// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include <cstdio>
#include <cmath>
#include <cstdint>

constexpr int YC_WAVETABLE_SIZE = 2048;
constexpr int YC_NUM_WAVE_TYPES = 5;
constexpr int YC_SINE_LUT_SIZE = 1024;
constexpr double YC_M_PI = 3.14159265358979323846;

static void print_floats(const float* data, int count)
{
    for (int i = 0; i < count; ++i) {
        if ((i % 8) == 0) std::printf("\n    ");
        std::printf("%.9g", (double)data[i]);
        if (i + 1 < count) std::printf(", ");
    }
}

int main()
{
    float yc_sine_lut[YC_SINE_LUT_SIZE];
    float yc_overdrive_lut[1024];
    float yc_softclip_lut[1024];
    float yc_footage_gain_lut[7];
    float yc_wavetables[YC_NUM_WAVE_TYPES][YC_WAVETABLE_SIZE];

    for (int i = 0; i < YC_SINE_LUT_SIZE; ++i) {
        yc_sine_lut[i] = sinf(2.0f * 3.14159265358979323846f * (float)i / (float)YC_SINE_LUT_SIZE);
    }

    float norm = tanhf(3.0f);
    for (int i = 0; i < 1024; ++i) {
        float x = (i / 1023.0f) * 2.0f - 1.0f;
        yc_overdrive_lut[i] = tanhf(x * 3.0f) / norm;
    }

    // Saettigungskurve des Soft-Clip-Limiters: tanh(t) fuer t in [0, 8].
    // yc_soft_clip() bildet t = (|x| - 0.9) / 0.1 darauf ab; tanh(8) ist in
    // float32 bereits exakt 1.0f, groessere t werden vom Aufrufer geclamped.
    for (int i = 0; i < 1024; ++i) {
        float t = (float)i / 1023.0f * 8.0f;
        yc_softclip_lut[i] = tanhf(t);
    }

    for (int i = 0; i < 7; ++i) {
        yc_footage_gain_lut[i] = powf((float)i / 6.0f, 1.2f);
    }

    for (int wave_type = 0; wave_type < YC_NUM_WAVE_TYPES; ++wave_type) {
        float max_val = 0.0f;
        for (int i = 0; i < YC_WAVETABLE_SIZE; ++i) {
            float sample = 0.0f;
            float phase = 2.0f * static_cast<float>(YC_M_PI) * i / YC_WAVETABLE_SIZE;
            if (wave_type == 0) {
                sample += 1.0f * sinf(phase * 1.0f);
                sample += 0.1f * sinf(phase * 2.0f);
                sample += 0.15f * sinf(phase * 3.0f);
                sample += 0.05f * sinf(phase * 4.0f);
            } else if (wave_type == 1) {
                for (int h = 1; h <= 25; h += 2) {
                    sample += (1.0f / (float)h) * sinf(phase * (float)h);
                }
            } else if (wave_type == 2) {
                for (int h = 1; h <= 25; ++h) {
                    sample += (1.0f / (float)h) * sinf(phase * (float)h);
                }
            } else if (wave_type == 3) {
                for (int h = 1; h <= 20; ++h) {
                    sample += (1.0f / sqrtf((float)h)) * sinf(phase * (float)h);
                }
            } else if (wave_type == 4) {
                for (int h = 1; h <= 15; h += 2) {
                    float coeff = (h <= 5) ? (1.0f / (float)h) : (1.0f / sqrtf((float)h));
                    sample += coeff * sinf(phase * (float)h);
                }
            }
            yc_wavetables[wave_type][i] = sample;
            if (fabsf(sample) > max_val) max_val = fabsf(sample);
        }
        if (max_val > 0.0f) {
            for (int i = 0; i < YC_WAVETABLE_SIZE; ++i) {
                yc_wavetables[wave_type][i] /= max_val;
            }
        }
    }

    std::printf("#pragma once\n");
    std::printf("#include <cstdint>\n");

    std::printf("inline const float yc_sine_lut[%d] = {", YC_SINE_LUT_SIZE);
    print_floats(yc_sine_lut, YC_SINE_LUT_SIZE);
    std::printf("\n};\n");

    std::printf("inline const float yc_overdrive_lut[1024] = {");
    print_floats(yc_overdrive_lut, 1024);
    std::printf("\n};\n");

    std::printf("inline const float yc_softclip_lut[1024] = {");
    print_floats(yc_softclip_lut, 1024);
    std::printf("\n};\n");

    std::printf("inline const float yc_footage_gain_lut[7] = {");
    print_floats(yc_footage_gain_lut, 7);
    std::printf("\n};\n");

    std::printf("inline const float yc_wavetables[%d][%d] = {", YC_NUM_WAVE_TYPES, YC_WAVETABLE_SIZE);
    for (int wave_type = 0; wave_type < YC_NUM_WAVE_TYPES; ++wave_type) {
        if (wave_type > 0) std::printf(",");
        std::printf("\n    {");
        print_floats(yc_wavetables[wave_type], YC_WAVETABLE_SIZE);
        std::printf("\n    }");
    }
    std::printf("\n};\n");

    return 0;
}
