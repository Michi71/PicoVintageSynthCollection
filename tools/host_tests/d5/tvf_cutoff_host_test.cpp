// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// The D-50's TVF base cutoff, pinned. IC25 0x0809-0x08EB composes it as
// 2 * panel + 54 chip units (plus bias and keyfollow, both /64), and
// 0x08CF-0x08DE caps it at 344 minus the pitch word's high byte -- 216 on
// C4, 16 less per octave. In the chip (munt's LA32 model, ported) the wave
// is a sine below the middle (128) and opens by an octave of harmonics per
// 16 units above it. So a sawtooth at panel 25 (104) is a sine, at panel 50
// (154) a filtered series, at panel 75 (204) nearly the full saw, and on C6
// panel 100 is no brighter than panel 75 because both hit the cap at 184.
// Until 03.09.2026 the base was the MT-32's panel + 78 (128 at panel 50),
// which left every partial more than an octave and a half too dull.
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "d5_engine/d5_synth_voice.h"

namespace {

constexpr float kSR = 32000.0f;
int g_failed = 0;

void check(bool ok, const char* what, double got) {
    std::printf("%s  %-52s %.1f\n", ok ? "pass" : "FAIL", what, got);
    if (!ok) ++g_failed;
}

// Harmonic h of the sawtooth (which sounds an octave above the square's
// pitch), in dB relative to its fundamental, after 0.25 s of settling.
struct Profile { double h[11]; };

Profile profile(int note, float cutoff) {
    d5::SynthSpec s;
    s.waveform = d5::Waveform::kSawtooth;
    s.pulse_width = 0.0f;
    s.cutoff = cutoff;
    s.resonance = 0.0f;
    s.tvf_env_depth = 0.0f;
    s.cutoff_keyfollow = 1.0f;
    s.pitch_keyfollow = 1.0f;
    s.tva_env.t[0] = 0.002f;
    s.tva_env.l[0] = s.tva_env.l[1] = s.tva_env.l[2] = 1.0f;
    s.tva_env.sustain = 1.0f;
    d5::SynthPartial v;
    v.note_on(s, static_cast<float>(note), 1.0f, kSR, 1.0f, note - 60);
    const int n = 32000, skip = 8000;
    std::vector<float> x(n);
    d5::Modulation m;
    for (int i = 0; i < n; ++i) {
        if ((i & 31) == 0) v.block_mod(m);
        x[i] = v.next();
    }
    const double f0 = 2.0 * 440.0 * std::pow(2.0, (note - 69) / 12.0);
    Profile p{};
    for (int h = 1; h <= 10; ++h) {
        double best = 0;
        for (double f = f0 * h * 0.98; f <= f0 * h * 1.02; f += 0.5) {
            std::complex<double> acc = 0;
            for (int i = skip; i < n; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2 * M_PI * (i - skip) / (n - skip));
                acc += x[i] * w * std::polar(1.0, -2 * M_PI * f * i / kSR);
            }
            best = std::fmax(best, std::abs(acc));
        }
        p.h[h] = best;
    }
    for (int h = 10; h >= 1; --h) p.h[h] = 20 * std::log10(p.h[h] / p.h[1] + 1e-12);
    return p;
}

}  // namespace

int main() {
    const Profile c25 = profile(60, 0.25f), c50 = profile(60, 0.5f), c75 = profile(60, 0.75f);
    const Profile hi75 = profile(84, 0.75f), hi100 = profile(84, 1.0f), lo100 = profile(60, 1.0f);
    check(c25.h[2] < -60.0, "panel 25 on C4 (chip 104): a sine, h2 below -60 dB", c25.h[2]);
    check(c50.h[2] > -13.0 && c50.h[2] < -8.0, "panel 50 on C4 (chip 154): h2 near -10.6 dB", c50.h[2]);
    check(c50.h[4] > -35.0 && c50.h[4] < -28.0, "panel 50 on C4 (chip 154): h4 near -31.5 dB", c50.h[4]);
    check(c75.h[10] > -27.0 && c75.h[10] < -20.0, "panel 75 on C4 (chip 204): h10 near -23.7 dB", c75.h[10]);
    check(std::fabs(hi100.h[10] - hi75.h[10]) < 0.5, "C6: panel 100 capped to panel 75 (both 184)", hi100.h[10] - hi75.h[10]);
    check(lo100.h[10] - hi100.h[10] > 4.0, "cap: C4 at panel 100 (216) brighter than C6 (184)", lo100.h[10] - hi100.h[10]);
    std::printf("%s\n", g_failed ? "FAILED" : "all pass");
    return g_failed ? 1 : 0;
}
