// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// The D-50's pulse width, pinned. The firmware writes T[panel] + velocity
// straight into the chip register (IC25 0x07E6-0x0805, 0x072A), and the
// chip halves the duty every 64 register units from a square at 0 --
// panel 0/25/50/75 are 50/25/12.5/6.25 % duty, the spectral nulls at
// h4, h8 and h16, and the floor is 1/16 -- as Roland's D-50 VST shows on
// a single square partial recorded dry. munt's MT-32 model keeps the
// wave symmetric up to register 128; this machine does not.
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "d5_engine/d5_synth_voice.h"

namespace {

constexpr float kSR = 32000.0f;
int g_failed = 0;

void check(bool ok, const char* what, double got) {
    std::printf("%s  %-50s %.1f\n", ok ? "pass" : "FAIL", what, got);
    if (!ok) ++g_failed;
}

// Harmonics 1..16 of a square partial (square-equivalent pitch C3, 130.8 Hz)
// at panel cutoff 90, in dB relative to the fundamental.
struct Profile { double h[17]; };

Profile profile(float pulse_width) {
    d5::SynthSpec s;
    s.waveform = d5::Waveform::kSquare;
    s.pulse_width = pulse_width;
    s.cutoff = 0.9f;
    s.resonance = 0.0f;
    s.tvf_env_depth = 0.0f;
    s.cutoff_keyfollow = 1.0f;
    s.pitch_keyfollow = 1.0f;
    s.tva_env.t[0] = 0.002f;
    s.tva_env.l[0] = s.tva_env.l[1] = s.tva_env.l[2] = 1.0f;
    s.tva_env.sustain = 1.0f;
    d5::SynthPartial v;
    v.note_on(s, 48.0f, 1.0f, kSR, 1.0f, 0);
    const int n = 32000, skip = 8000;
    std::vector<float> x(n);
    d5::Modulation m;
    for (int i = 0; i < n; ++i) {
        if ((i & 31) == 0) v.block_mod(m);
        x[i] = v.next();
    }
    const double f0 = 130.8128;
    Profile p{};
    for (int h = 1; h <= 16; ++h) {
        double best = 0;
        for (double f = f0 * h * 0.99; f <= f0 * h * 1.01; f += 0.25) {
            std::complex<double> acc = 0;
            for (int i = skip; i < n; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2 * M_PI * (i - skip) / (n - skip));
                acc += x[i] * w * std::polar(1.0, -2 * M_PI * f * i / kSR);
            }
            best = std::fmax(best, std::abs(acc));
        }
        p.h[h] = best;
    }
    for (int h = 16; h >= 1; --h) p.h[h] = 20 * std::log10(p.h[h] / p.h[1] + 1e-12);
    return p;
}

}  // namespace

int main() {
    const Profile p0 = profile(0.0f), p25 = profile(0.25f), p50 = profile(0.5f), p75 = profile(0.75f), p100 = profile(1.0f);
    check(p0.h[2] < -50.0, "panel 0: a square, h2 absent", p0.h[2]);
    check(p25.h[4] < -35.0, "panel 25: 25 % duty, null at h4", p25.h[4]);
    check(p25.h[2] > -6.0 && p25.h[2] < -1.0, "panel 25: h2 near -3 dB", p25.h[2]);
    check(p50.h[8] < -35.0, "panel 50: 12.5 % duty, null at h8", p50.h[8]);
    check(p50.h[4] > -6.0, "panel 50: h4 still strong", p50.h[4]);
    check(p75.h[16] < -30.0, "panel 75: 6.25 % duty, null at h16", p75.h[16]);
    check(p100.h[16] < -30.0 && std::fabs(p100.h[8] - p75.h[8]) < 1.0, "panel 100: floor, same as panel 75 (h8)", p100.h[8] - p75.h[8]);
    std::printf("%s\n", g_failed ? "FAILED" : "all pass");
    return g_failed ? 1 : 0;
}
