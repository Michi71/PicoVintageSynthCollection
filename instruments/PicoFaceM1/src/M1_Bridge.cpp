// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "M1_Bridge.h"

#include <cstring>

#include "m1_rom_tables.h"   // generated at configure time from the user's ROMs

#if defined(TARGET_RP2350) || defined(PICO_BUILD)
#  include "pico/time.h"
static inline uint32_t m1_time_us_32() { return time_us_32(); }
#else
#  include <chrono>
static inline uint32_t m1_time_us_32() {
    static auto epoch = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - epoch).count();
}
#endif

// The PCM blob comes from tools/m1_extract/m1_make_blob.py via .incbin; see
// instrument.cmake. Nothing ROM-derived is in the repository.
extern "C" {
extern const uint8_t m1_pcm_blob[];
extern const uint8_t m1_pcm_blob_end[];
}

namespace {
// The MDE's 64 K words of delay RAM: 128 KB.
int16_t g_fxMem[65536];
// The interpolation kernel is read at a random phase for every voice and every
// sample; from flash that would fight the PCM stream for the 16 KB XIP cache.
// The VDF coefficient tables are read per sample during ramps for the same
// reason. 48 KB of RAM between them.
uint8_t  g_kernel[sizeof(m1rom::kKernel)];
uint16_t g_vdf1k[4096], g_vdf2k[4096];
} // namespace

void M1_Bridge::init() {
    memcpy(g_kernel, m1rom::kKernel, sizeof g_kernel);
    memcpy(g_vdf1k, m1rom::kVdf1k, sizeof g_vdf1k);
    memcpy(g_vdf2k, m1rom::kVdf2k, sizeof g_vdf2k);
    m1::RomTables rom{};
    rom.pcm = reinterpret_cast<const int16_t*>(m1_pcm_blob);
    rom.pcmWords = (uint32_t)((m1_pcm_blob_end - m1_pcm_blob) / 2);
    rom.ms = m1rom::kMultisounds;
    rom.splits = m1rom::kSplits;
    rom.drums = m1rom::kDrums;
    rom.nDrums = m1rom::kDrumCount;
    rom.kernel = g_kernel;
    rom.vdf1k = g_vdf1k;
    rom.vdf2k = g_vdf2k;
    rom.egRise = m1rom::kEgRise;
    rom.egFall = m1rom::kEgFall;
    rom.velCurve = m1rom::kVelCurve;
    rom.mgFreq = m1rom::kMgFreq;
    rom.mgDelay = m1rom::kMgDelay;
    rom.mgIntP = m1rom::kMgIntP;
    rom.mgIntF = m1rom::kMgIntF;
    rom.pitchS = m1rom::kPitchS;
    rom.pitchF = m1rom::kPitchF;
    rom.panReg = m1rom::kPanReg;
    rom.pan101 = m1rom::kPan101;
    rom.delayStart = m1rom::kDelayStart;
    rom.fx = &m1rom::kFx;
    rom.programs = m1rom::kPrograms;
    rom.combis = m1rom::kCombis;
    rom.kits = m1rom::kKits;
    engine_.init(rom, g_fxMem);
    engine_.selectProgram(0);
}

void M1_Bridge::selectProgram(int i) { engine_.allSoundOff(); engine_.selectProgram(i); }
void M1_Bridge::selectCombi(int i)   { engine_.allSoundOff(); engine_.selectCombi(i); }

void M1_Bridge::setVolume(uint8_t percent) {
    if (percent > 100) percent = 100;
    const float x = percent * 0.01f;
    gain_ = x * x;                      // perceptual taper, as the other instruments
}

void M1_Bridge::setMidiVolume(uint8_t v) {
    if (v > 127) v = 127;
    const float x = v / 127.0f;
    midiGain_ = x * x;
}

void M1_Bridge::fillBufferI32(int32_t* out, int frames) {
    const uint32_t t0 = m1_time_us_32();
    int done = 0;
    // The M1's 16 voices sum well below full scale (one voice peaks around
    // -12 dBFS); the output stage gives that headroom back.
    const float g = gain_ * midiGain_ * 2.0f;
    while (done < frames) {
        int chunk = frames - done;
        if (chunk > kBlock) chunk = kBlock;
        engine_.render(bufL_, bufR_, chunk);
        for (int i = 0; i < chunk; ++i) {
            float l = bufL_[i] * g, r = bufR_[i] * g;
            const float a = 0.9f, range = 1.0f - a;
            if (l > a)       l =  a + range * (1.0f - 1.0f / (1.0f + (l - a) / range));
            else if (l < -a) l = -a - range * (1.0f - 1.0f / (1.0f + (-l - a) / range));
            if (r > a)       r =  a + range * (1.0f - 1.0f / (1.0f + (r - a) / range));
            else if (r < -a) r = -a - range * (1.0f - 1.0f / (1.0f + (-r - a) / range));
            int32_t dl = (int32_t)(l * 32767.0f), dr = (int32_t)(r * 32767.0f);
            if (dl >  32767) dl =  32767; else if (dl < -32768) dl = -32768;
            if (dr >  32767) dr =  32767; else if (dr < -32768) dr = -32768;
            // Two int32 words per frame, sample in the upper half (see JV_Bridge).
            out[2 * (done + i)]     = dl << 16;
            out[2 * (done + i) + 1] = dr << 16;
        }
        done += chunk;
    }
    const float budget = (float)frames * 1.0e6f / (float)m1::kSampleRate;
    if (budget > 0.0f) {
        cpuLoad_ = (float)(m1_time_us_32() - t0) / budget * 100.0f;
        cpuPeak_ *= 0.999f;
        if (cpuLoad_ > cpuPeak_) cpuPeak_ = cpuLoad_;
    }
}
