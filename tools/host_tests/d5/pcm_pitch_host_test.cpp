// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// The D-50's PCM pitch law, pinned. A PCM partial advances through its
// sample at f/250 words per output sample, where f is the frequency a
// square partial with the same bytes would sound at: the firmware subtracts
// four octaves from the pitch word for PCM (IC25 0x0F49) and the chip's PCM
// reference is 2048 words per period (munt LA32WaveGenerator). So a 128-word
// cycle played at C4 sounds at C4 and a 2048-word cycle at C0, whatever
// material the cycle holds. Issue #142 was this law being replaced by the
// measured pitch of each sample. No ROM is needed: synthetic cycles suffice.
#include <cmath>
#include <cstdio>
#include <vector>

#include "d5_engine/d5_pcm_voice.h"

namespace {

constexpr float kSR = 32000.0f;
int g_failed = 0;

void check(bool ok, const char* what, double got, double want) {
    std::printf("%s  %-46s got %.3f want %.3f\n", ok ? "pass" : "FAIL", what, got, want);
    if (!ok) ++g_failed;
}

// One cycle of a sine over `words` samples, as int16.
std::vector<int16_t> cycle(unsigned words) {
    std::vector<int16_t> v(words);
    for (unsigned i = 0; i < words; ++i)
        v[i] = static_cast<int16_t>(std::lround(20000.0 * std::sin(2.0 * M_PI * i / words)));
    return v;
}

// Frequency from rising zero crossings over `seconds` of output.
double sounding_hz(const std::vector<int16_t>& data, float note, double seconds) {
    d5::PcmSampleRef ref;
    ref.data = data.data();
    ref.start = 0;
    ref.length = static_cast<uint32_t>(data.size());
    ref.looped = true;
    d5::Env5Spec env;                       // full level, held: the shape is not under test
    env.t[0] = 0.001f;
    env.l[0] = env.l[1] = env.l[2] = 1.0f;
    env.sustain = 1.0f;
    d5::PcmVoice v;
    v.note_on(ref, note, 1.0f, env, kSR);
    const int n = static_cast<int>(kSR * seconds);
    const int skip = static_cast<int>(kSR * 0.05f);
    float prev = 0.0f;
    int crossings = 0, first = -1, last = -1;
    for (int i = 0; i < n; ++i) {
        const float x = v.next();
        if (i >= skip && prev < 0.0f && x >= 0.0f) {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
        prev = x;
    }
    if (crossings < 2) return 0.0;
    return (crossings - 1) * static_cast<double>(kSR) / (last - first);
}

}  // namespace

int main() {
    check(d5::kPcmPitchRefHz == 250.0f, "reference: 32000 / 128 = 250 Hz", d5::kPcmPitchRefHz, 250.0);

    const double c4 = 261.6256, c0 = c4 / 16.0, c5 = 2.0 * c4;
    const auto w128 = cycle(128), w2048 = cycle(2048), w32 = cycle(32);

    double f = sounding_hz(w128, 60.0f, 2.0);
    check(std::fabs(f / c4 - 1.0) < 0.002, "128-word cycle at C4 sounds C4 (stored rate)", f, c4);
    f = sounding_hz(w128, 72.0f, 2.0);
    check(std::fabs(f / c5 - 1.0) < 0.002, "an octave up on the key doubles it", f, c5);
    f = sounding_hz(w2048, 60.0f, 4.0);
    check(std::fabs(f / c0 - 1.0) < 0.01, "2048-word cycle at C4 sounds C0 (Spect loops)", f, c0);
    f = sounding_hz(w32, 60.0f, 2.0);
    check(std::fabs(f / (4.0 * c4) - 1.0) < 0.002, "32-word cycle at C4 sounds C6 (FluteH class)", f, 4.0 * c4);

    std::printf("%s\n", g_failed ? "FAILED" : "all pass");
    return g_failed ? 1 : 0;
}
