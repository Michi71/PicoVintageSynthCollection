// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "RD_Synth_Bridge_v2.h"
#include <cstring>
#include <cmath>

#if defined(TARGET_RP2350) || defined(PICO_BUILD)
#  include "ram_hot.h"
#  include "pico/time.h"
static inline uint32_t bridge_time_us_32() { return time_us_32(); }
#else
#  define RAM_HOT(x) x
#  include <chrono>
static inline uint32_t bridge_time_us_32()
{
    static auto epoch = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - epoch).count());
}
#endif

// keep in sync -- duplicates from mcu.cpp (mcu.cpp is not linked into the v2 target)
static const int s_sampleRatesAll[16] = {
    20000, 20000, 20000, 32000,
    32000, 20000, 20000, 32000,
    20000, 20000, 20000, 32000,
    20000, 20000, 32000, 20000
};

// keep in sync -- duplicates from mcu.cpp
static const char* const s_patchNamesAll[16] = {
    "MKS-20: Piano 1",
    "MKS-20: Piano 2",
    "MKS-20: Piano 3",
    "MKS-20: Harpsichord",
    "MKS-20: Clavi",
    "MKS-20: Vibraphone",
    "MKS-20: E-Piano 1",
    "MKS-20: E-Piano 2",
    "MK-80: Classic",
    "MK-80: Special",
    "MK-80: Blend",
    "MK-80: Contemporary",
    "MK-80: A. Piano 1",
    "MK-80: A. Piano 2",
    "MK-80: Clavi",
    "MK-80: Vibraphone"
};

// This build's slice of the two tables above. They stay whole -- they are a
// copy of mcu.cpp's and are worth keeping faithful for a few hundred bytes --
// and everything below indexes them through the base, so a single-machine
// build reads its own half without any of it knowing.
static const int* const s_sampleRates = s_sampleRatesAll + RD_PATCH_BASE;
static const char* const* const s_patchNames = s_patchNamesAll + RD_PATCH_BASE;


void RD_Synth_Bridge::setInstrumentInternal(uint8_t id)
{
    if (id >= RD_PATCH_COUNT) id = 0;
    engineReady_ = engine_.loadPack(rd_pack_ptrs[id], rd_pack_sizes[id]);
    // Voice governor: Auto base = proven per-rate caps (32-kHz patches have
    // 1.6x less budget per sample -> 12; else 16). Manual modes keep the
    // user's fixed choice. Stolen voices are masked by the new attack.
    instrument_ = id;                                  // set first so autoBaseLimit() reads the new rate
    if (voiceMode_ == kVoiceModeAuto) {
        autoLimit_ = autoBaseLimit();
        autoHoldSamples_ = 0;
    }
    applyVoiceMode(false);
}

const uint8_t RD_Synth_Bridge::s_voiceModeTable[4] = {8, 16, 24, 32};

uint8_t RD_Synth_Bridge::autoBaseLimit() const
{
    // Proven per-rate caps; instrument_ must already reflect the new rate.
    return (s_sampleRates[instrument_] == 32000) ? 12 : 16;
}

void RD_Synth_Bridge::applyVoiceMode(bool resetAuto)
{
    uint8_t newLimit;
    if (voiceMode_ == kVoiceModeAuto) {
        if (resetAuto) {
            autoLimit_ = autoBaseLimit();
            autoHoldSamples_ = 0;
        } else {
            // Keep autoLimit_ within sane bounds if base shifted with rate.
            uint8_t base = autoBaseLimit();
            if (autoLimit_ < kAutoFloor) autoLimit_ = kAutoFloor;
            if (autoLimit_ > base) autoLimit_ = base;
        }
        newLimit = autoLimit_;
    } else {
        // Manual modes: user's explicit choice, independent of rate.
        newLimit = s_voiceModeTable[voiceMode_];
    }

    if (newLimit != effectiveLimit_) {
        effectiveLimit_ = newLimit;
        engine_.setVoiceLimit(newLimit);
    }
}

void RD_Synth_Bridge::setVoiceMode(uint8_t mode)
{
    if (mode > kVoiceModeAuto) mode = kVoiceModeAuto;
    if (mode == voiceMode_) return;
    voiceMode_ = mode;
    applyVoiceMode(true);
}

void RD_Synth_Bridge::governorTick(float load, int length)
{
    if (voiceMode_ != kVoiceModeAuto) return;

    // Cut: overload -> drop limit immediately, restart recovery hold.
    if (load >= 90.0f) {
        if (autoLimit_ > kAutoFloor) {
            autoLimit_ = (autoLimit_ >= kAutoFloor + 6) ? (autoLimit_ - 6) : kAutoFloor;
            applyVoiceMode(false);
        }
        autoHoldSamples_ = 0;
        // Re-cull every overloaded tick: stolen slots may resurrect condemned voices.
        engine_.setVoiceLimit(autoLimit_);
        return;
    }

    // Recovery: cool load, hold expired, still below base -> +1, restart hold.
    if (load < 70.0f && autoLimit_ < autoBaseLimit()) {
        if (autoHoldSamples_ <= 0) {
            // 700 ms of rendered samples at current rate.
            autoHoldSamples_ = (int32_t)s_sampleRates[instrument_] * 7 / 10;
        }
        autoHoldSamples_ -= length;
        if (autoHoldSamples_ <= 0) {
            autoLimit_ += 1;
            applyVoiceMode(false);
            autoHoldSamples_ = 0;
        }
    }
}

void RD_Synth_Bridge::voiceGovernorEmergency()
{
    if (voiceMode_ != kVoiceModeAuto) return;
    // Underrun already audible -> go straight to the floor.
    if (autoLimit_ > kAutoFloor) {
        autoLimit_ = kAutoFloor;
        applyVoiceMode(false);
    }
    autoHoldSamples_ = 0;
    // Re-cull even at unchanged floor: stolen slots may resurrect condemned voices.
    engine_.setVoiceLimit(autoLimit_);
}

void RD_Synth_Bridge::init()
{
    setInstrumentInternal(0);
    fx_.init((float)s_sampleRates[instrument_]);
    pedal_ = false;
    deferredOff_[0] = deferredOff_[1] = deferredOff_[2] = deferredOff_[3] = 0;
    noteOnCount_ = 0;
    sampleRateChanged_ = false;
    cpuLoadPercent_ = 0;
    cpuLoadPeakPercent_ = 0;
    voiceMode_ = kVoiceModeAuto;
    autoLimit_ = autoBaseLimit();
    autoHoldSamples_ = 0;
    effectiveLimit_ = 0;          // force first apply to push to engine
    applyVoiceMode(false);
}

void RD_Synth_Bridge::setInstrument(uint8_t id)
{
    if (id > 15) return;
    if (id == instrument_ && engineReady_) return;

    int oldRate = s_sampleRates[instrument_];
    setInstrumentInternal(id);

    int newRate = s_sampleRates[id];
    if (newRate != oldRate)
    {
        fx_.setSampleRate((float)newRate);
        sampleRateChanged_ = true;
    }
}

void RAM_HOT(RD_Synth_Bridge::fill_buffer_i32)(int32_t* out, int length)
{
    uint32_t t0 = bridge_time_us_32();

    const float a     = 0.9f;
    const float range = 1.0f - a;   // same rational softclip as the v1 bridge

    static int32_t accBuf[64];
    int done = 0;
    while (done < length)
    {
        int chunk = length - done;
        if (chunk > 64) chunk = 64;
        for (int k = 0; k < chunk; ++k) accBuf[k] = 0;
        engine_.renderBlock(accBuf, chunk);   // ONE core-1 rendezvous per chunk

    for (int i = 0; i < chunk; ++i)
    {
        float f = accBuf[i] * (1.0f / 131072.0f);

        float l, r;
        fx_.process(f, &l, &r);

        if (l > a)       l =  a + range * (1.0f - 1.0f / (1.0f + (l - a) / range));
        else if (l < -a) l = -a - range * (1.0f - 1.0f / (1.0f + (-l - a) / range));
        if (r > a)       r =  a + range * (1.0f - 1.0f / (1.0f + (r - a) / range));
        else if (r < -a) r = -a - range * (1.0f - 1.0f / (1.0f + (-r - a) / range));

        int32_t dl = (int32_t)(l * 32767.0f);
        int32_t dr = (int32_t)(r * 32767.0f);
        if (dl >  32767) dl =  32767;
        if (dl < -32768) dl = -32768;
        if (dr >  32767) dr =  32767;
        if (dr < -32768) dr = -32768;

        out[2 * (done + i)]     = dl << 16;
        out[2 * (done + i) + 1] = dr << 16;
    }
        done += chunk;
    }

    uint32_t elapsed = bridge_time_us_32() - t0;
    float budget = (float)length * 1.0e6f / (float)s_sampleRates[instrument_];
    float load = budget > 0.0f ? ((float)elapsed / budget) * 100.0f : 0.0f;
    cpuLoadPercent_ = load;
    if (load > cpuLoadPeakPercent_) cpuLoadPeakPercent_ = load;
    governorTick(load, length);  // sample-count deterministic Auto governor
}

void RD_Synth_Bridge::pitchBend(uint16_t bend14) {
    if (!engineReady_) return;

    // MK-80 default bender depth: +-2 semitones across the full 14-bit range.
    float semis  = ((int)bend14 - 8192) * (2.0f / 8192.0f);
    float factor = exp2f(semis / 12.0f);
    uint32_t q16 = (uint32_t)(factor * 65536.0f + 0.5f);

    engine_.setPitchBend(q16);
}

void RD_Synth_Bridge::setMasterTune(int cents) {
    if (cents < -50) cents = -50;
    else if (cents > 50) cents = 50;
    masterTuneCents_ = (int8_t)cents;
    uint32_t q16 = (uint32_t)(exp2f((float)cents / 1200.0f) * 65536.0f + 0.5f);
    engine_.setMasterTune(q16);
}

void RD_Synth_Bridge::instrumentName(char* out, uint32_t maxLen)
{
    if (maxLen == 0) return;
    strncpy(out, s_patchNames[instrument_], maxLen);
    out[maxLen - 1] = '\0';
}

uint32_t RD_Synth_Bridge::currentSampleRate() const
{
    return (uint32_t)s_sampleRates[instrument_];
}

bool RD_Synth_Bridge::consumeSampleRateChanged()
{
    bool v = sampleRateChanged_;
    sampleRateChanged_ = false;
    return v;
}

void RD_Synth_Bridge::setFxParam(uint8_t id, uint8_t val255)
{
    fx_.setParam(id, val255 * (1.0f / 255.0f));
}
