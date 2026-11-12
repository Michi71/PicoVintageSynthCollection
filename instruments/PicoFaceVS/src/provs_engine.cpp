// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "provs/engine.h"

#include <cmath>

#include "provs/fw_tables.h"
#include "provs/preset_map.h"
#if PROVS_HAS_PVM
#include "provs/pvm_map.h"
#endif

namespace provs {

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void Engine::init(double sampleRate) {
    sr_ = float(sampleRate);
    fx_.init(sampleRate);
    allNotesOff();
    for (auto& layer : lfo_) layer[0] = layer[1] = Lfo{};
    ctl_ = 0;
    applyParams();
}

void Engine::setParams(const Params& p) { pNext_ = p; paramsDirty_ = true; }
void Engine::setLayer2(const Params& p) { p2Next_ = p; paramsDirty_ = true; }

void Engine::applyParams() {
    p_ = pNext_;
    p2_ = p2Next_;
    paramsDirty_ = false;
}

// --- Programs ----------------------------------------------------------------

// The bank lives in RAM: 100 programs of 116 fields each (one byte per field).
namespace {
constexpr int kBankSize = 100;
constexpr int kFields = 116;
constexpr int kProgramBytes = 82;        // bit-packed
uint8_t gBank[kBankSize][kFields];
int gBankCount = 0;                      // 0 = not set up yet

void bankInit() {
    for (auto& prog : gBank)
        for (int i = 0; i < kFields; ++i) prog[i] = kInitProgram[i];
    gBankCount = 1;
}
inline void bankNeed() { if (gBankCount == 0) bankInit(); }

// 164 nibbles (high nibble first) -> 82 bytes -> 116 fields
bool unpackProgram(const uint8_t* nib, uint8_t* fields) {
    uint8_t rec[kProgramBytes];
    for (int i = 0; i < kProgramBytes; ++i) {
        if ((nib[2 * i] | nib[2 * i + 1]) & 0xF0) return false;
        rec[i] = uint8_t((nib[2 * i] << 4) | nib[2 * i + 1]);
    }
    Engine::unpackVsProgram(rec, fields);
    return true;
}
}  // namespace

void Engine::resetVsBank() { bankInit(); }

int Engine::vsFieldBits(int field) {
    return field >= 0 && field < kFields ? kProgramFieldBits[field] : 0;
}

// 82 bytes -> 116 fields, MSB first by the firmware's width table
void Engine::unpackVsProgram(const uint8_t* rec, uint8_t* fields) {
    int bit = 0;
    for (int i = 0; i < kFields; ++i) {
        int v = 0;
        for (int b = 0; b < kProgramFieldBits[i]; ++b, ++bit)
            v = (v << 1) | ((rec[bit >> 3] >> (7 - (bit & 7))) & 1);
        fields[i] = uint8_t(v);
    }
}

void Engine::packVsProgram(const uint8_t* fields, uint8_t* rec) {
    for (int i = 0; i < kProgramBytes; ++i) rec[i] = 0;
    int bit = 0;
    for (int i = 0; i < kFields; ++i)
        for (int b = kProgramFieldBits[i] - 1; b >= 0; --b, ++bit)
            if ((fields[i] >> b) & 1) rec[bit >> 3] |= uint8_t(0x80 >> (bit & 7));
}

void Engine::dumpVsNibbles(const uint8_t* fields, uint8_t* out) {
    uint8_t rec[kProgramBytes];
    packVsProgram(fields, rec);
    for (int i = 0; i < kProgramBytes; ++i) {
        out[2 * i] = rec[i] >> 4;
        out[2 * i + 1] = rec[i] & 15;
    }
}

size_t Engine::dumpVsProgram(const uint8_t* fields, int slot, uint8_t* out) {
    out[0] = 0xF0; out[1] = 0x01; out[2] = 0x0A; out[3] = uint8_t(slot < 0 ? 0 : (slot > 99 ? 99 : slot));
    dumpVsNibbles(fields, out + 4);
    out[kVsDumpBytes - 1] = 0xF7;
    return kVsDumpBytes;
}

const uint8_t* Engine::vsProgram(int index) {
    bankNeed();
    return index >= 0 && index < gBankCount ? gBank[index] : kInitProgram;
}

bool Engine::storeVsProgram(int index, const uint8_t* fields) {
    bankNeed();
    if (index < 0 || index >= kBankSize) return false;
    for (int i = 0; i < kFields; ++i)
        gBank[index][i] = uint8_t(fields[i] & ((1 << kProgramFieldBits[i]) - 1));
    if (index + 1 > gBankCount) gBankCount = index + 1;
    return true;
}

void Engine::setVsBankCount(int count) {
    bankNeed();
    gBankCount = count < 1 ? 1 : (count > kBankSize ? kBankSize : count);
}

int Engine::loadVsSysex(const uint8_t* d, size_t n) {
    bankNeed();
    constexpr size_t kNib = 2 * kProgramBytes;
    int loaded = 0;
    size_t i = 0;
    while (i + 4 <= n) {
        if (d[i] != 0xF0) { ++i; continue; }
        if (d[i + 1] != 0x01 || d[i + 2] != 0x0A || d[i + 3] > kBankSize) { ++i; continue; }
        const int slot = d[i + 3];
        const int count = slot == kBankSize ? kBankSize : 1;
        const size_t end = i + 4 + size_t(count) * kNib;      // position of the F7
        if (end >= n || d[end] != 0xF7) { ++i; continue; }
        // validate completely first (only nibbles allowed), then accept
        bool ok = true;
        for (size_t k = i + 4; k < end && ok; ++k) ok = (d[k] & 0xF0) == 0;
        if (ok) {
            const int first = count == kBankSize ? 0 : slot;
            for (int k = 0; k < count; ++k) unpackProgram(d + i + 4 + size_t(k) * kNib, gBank[first + k]);
            if (count == kBankSize) gBankCount = kBankSize;
            else if (slot + 1 > gBankCount) gBankCount = slot + 1;
            loaded += count;
        }
        i = end + 1;
    }
    return loaded;
}

int Engine::presetCount() { bankNeed(); return gBankCount; }

void Engine::setPreset(int index) {
    bankNeed();
    if (index < 0 || index >= gBankCount) return;
    setVsProgram(gBank[index]);
}

void Engine::setVsProgram(const uint8_t* pr) {
    bankNeed();
    Params p = pNext_;
    mapVsPreset(pr, p);
    // Split/double: field 61 names the linked program for the second layer
    if (p.keyMode != kKeyNormal) {
        const int link = pr[61] < gBankCount ? pr[61] : 0;
        Params q;
        mapVsPreset(gBank[link], q);
        setLayer2(q);
    }
    setParams(p);
}

void Engine::presetName(int index, char* buf) {
    bankNeed();
    for (int i = 0; i < 8; ++i) buf[i] = ' ';
    buf[8] = 0;
    if (index < 0 || index >= gBankCount) return;
    for (int i = 0; i < 8; ++i) buf[i] = kNameChars[gBank[index][78 + i] & 31];
}

#if PROVS_HAS_PVM
namespace {
constexpr int kPvmBankSize = 32;
constexpr int kPvmPatchBytes = 64;
constexpr int kPvmHeader = 8;
uint8_t gPvmBank[kPvmBankSize][kPvmPatchBytes];
char gPvmNames[kPvmBankSize][9];
int gPvmCount = 0;
}  // namespace

void Engine::resetPvmBank() { gPvmCount = 0; }

int Engine::loadPvmSysex(const uint8_t* d, size_t n) {
    if (!kPvmHasEnvTable) return 0;
    constexpr size_t kLen = kPvmHeader + kPvmBankSize * kPvmPatchBytes + 2;   // check byte, F7
    for (size_t i = 0; i + kLen <= n; ++i) {
        if (d[i] != 0xF0 || d[i + 1] != 0x00 || d[i + 2] != 0x20 || d[i + 3] != 0x32) continue;
        if (d[i + kLen - 1] != 0xF7) continue;
        const uint8_t* src = d + i + kPvmHeader;
        for (int k = 0; k < kPvmBankSize; ++k) {
            for (int b = 0; b < kPvmPatchBytes; ++b) gPvmBank[k][b] = src[k * kPvmPatchBytes + b];
            for (int c = 0; c < 8; ++c) {
                const uint8_t ch = gPvmBank[k][55 + c];
                gPvmNames[k][c] = (ch >= 32 && ch < 127) ? char(ch) : ' ';
            }
            gPvmNames[k][8] = 0;
        }
        gPvmCount = kPvmBankSize;
        return kPvmBankSize;
    }
    return 0;
}

int Engine::pvmPatchCount() { return gPvmCount; }

const char* Engine::pvmPatchName(int index) {
    if (index < 0 || index >= gPvmCount) return "";
    return gPvmNames[index];
}

void Engine::setPvmPatch(int index) {
    if (index < 0 || index >= gPvmCount) return;
    Params p = pNext_;
    mapPvmPatch(gPvmBank[index], p);
    setParams(p);
}
#else
void Engine::resetPvmBank() {}
int Engine::loadPvmSysex(const uint8_t*, size_t) { return 0; }
int Engine::pvmPatchCount() { return 0; }
const char* Engine::pvmPatchName(int) { return ""; }
void Engine::setPvmPatch(int) {}
#endif

// --- User waves ----------------------------------------------------------------
// Waves 0..31 are RAM on the original (NVRAM @0xFFFFA008, 192 bytes each).
// Here a slot that has been written plays from the tables below; one that has
// not plays what the build put there - the Pro VS Mini's wave, or silence.

namespace {
constexpr int kUserWaveCount = Engine::kUserWaves;
static_assert(Engine::kWaveSamples == kWaveFrames, "wave length");
int16_t gUserRaw[kUserWaveCount][kWaveFrames];                 // 12 bit signed, as written
int16_t gUserMips[kUserWaveCount][kWaveMips][kWaveFrames];     // amplitude 1 = kUserUnit
uint32_t gUserMask = 0;          // slots that have been written
uint32_t gUserPending = 0;       // slots whose band-limited levels are out of date
// Half the int16 range for full scale: band-limiting overshoots at edges.
constexpr float kUserUnit = 16384.0f;

// One cycle of sine in 128 steps. Every harmonic of a 128-sample wave lands
// on these points, so analysis and synthesis need no other values.
float gSine[kWaveFrames];
bool gSineReady = false;

// The wave whose levels are being computed, harmonic by harmonic.
int gBuildSlot = -1;
int gBuildHarm = 0;                               // the next harmonic
float gBuildAcc[kWaveFrames];                     // sum of the harmonics so far
int16_t gBuildMips[kWaveMips][kWaveFrames];

inline int16_t toUserUnit(float v12) {
    const float v = v12 * (kUserUnit / 2048.0f);
    return int16_t(v < -32768.0f ? -32768 : (v > 32767.0f ? 32767 : std::lround(v)));
}
}  // namespace

void Engine::setUserWave(int slot, const int16_t* samples) {
    if (slot < 0 || slot >= kUserWaveCount) return;
    for (int n = 0; n < kWaveFrames; ++n) {
        const int v = samples[n];
        gUserRaw[slot][n] = int16_t(v < -2048 ? -2048 : (v > 2047 ? 2047 : v));
    }
    // A slot that is new has nothing to go on playing: start it on the wave
    // itself until the band-limited levels are there.
    if (!((gUserMask >> slot) & 1u))
        for (int m = 0; m < kWaveMips; ++m)
            for (int n = 0; n < kWaveFrames; ++n) gUserMips[slot][m][n] = int16_t(gUserRaw[slot][n] * 8);
    gUserMask |= 1u << slot;
    gUserPending |= 1u << slot;
}

void Engine::clearUserWave(int slot) {
    if (slot < 0 || slot >= kUserWaveCount) return;
    gUserMask &= ~(1u << slot);
    gUserPending &= ~(1u << slot);
    if (gBuildSlot == slot) gBuildSlot = -1;
}

void Engine::clearUserWaves() {
    gUserMask = gUserPending = 0;
    gBuildSlot = -1;
}

uint32_t Engine::userWaveMask() { return gUserMask; }

const int16_t* Engine::userWaveTable(int slot, int level) {
    return gUserMips[slot & (kUserWaveCount - 1)][level & (kWaveMips - 1)];
}

bool Engine::userWave(int slot, int16_t* out) {
    const bool set = slot >= 0 && slot < kUserWaveCount && ((gUserMask >> slot) & 1u);
    for (int n = 0; n < kWaveFrames; ++n) out[n] = set ? gUserRaw[slot][n] : int16_t(0);
    return set;
}

// Level 0 is the wave itself. Levels 1..3 keep its mean and the harmonics up
// to 31, 15 and 7, like the tables the build makes for the ROM waves.
bool Engine::buildUserWaves(int harmonics) {
    if (!gSineReady) {
        for (int n = 0; n < kWaveFrames; ++n)
            gSine[n] = std::sin(6.283185307f * float(n) / float(kWaveFrames));
        gSineReady = true;
    }
    while (harmonics > 0) {
        if (gBuildSlot >= 0 && ((gUserPending >> gBuildSlot) & 1u)) gBuildSlot = -1;   // written again: start over
        if (gBuildSlot < 0) {
            if (!gUserPending) return false;
            int slot = 0;
            while (!((gUserPending >> slot) & 1u)) ++slot;
            gUserPending &= ~(1u << slot);
            gBuildSlot = slot;
            gBuildHarm = 1;
            float mean = 0.0f;
            for (int n = 0; n < kWaveFrames; ++n) mean += float(gUserRaw[slot][n]);
            mean *= 1.0f / float(kWaveFrames);
            for (int n = 0; n < kWaveFrames; ++n) {
                gBuildAcc[n] = mean;
                gBuildMips[0][n] = int16_t(gUserRaw[slot][n] * 8);
            }
        }
        const int16_t* raw = gUserRaw[gBuildSlot];
        const int h = gBuildHarm;
        float re = 0.0f, im = 0.0f;
        for (int n = 0, i = 0; n < kWaveFrames; ++n, i = (i + h) & (kWaveFrames - 1)) {
            re += float(raw[n]) * gSine[(i + kWaveFrames / 4) & (kWaveFrames - 1)];
            im += float(raw[n]) * gSine[i];
        }
        re *= 2.0f / float(kWaveFrames);
        im *= 2.0f / float(kWaveFrames);
        for (int n = 0, i = 0; n < kWaveFrames; ++n, i = (i + h) & (kWaveFrames - 1))
            gBuildAcc[n] += re * gSine[(i + kWaveFrames / 4) & (kWaveFrames - 1)] + im * gSine[i];
        --harmonics;
        for (int m = 1; m < kWaveMips; ++m)
            if (h == kMipHarmonics[m])
                for (int n = 0; n < kWaveFrames; ++n) gBuildMips[m][n] = toUserUnit(gBuildAcc[n]);
        if (++gBuildHarm > kMipHarmonics[1]) {
            for (int m = 0; m < kWaveMips; ++m)
                for (int n = 0; n < kWaveFrames; ++n) gUserMips[gBuildSlot][m][n] = gBuildMips[m][n];
            gBuildSlot = -1;
        }
    }
    return gBuildSlot >= 0 || gUserPending != 0;
}

void Engine::waveSamples(int wave, int16_t* out) {
    for (int n = 0; n < kWaveFrames; ++n) out[n] = 0;
    if (wave < 0 || wave >= kWaveCountVS) return;            // 127 = noise: nothing to read
    if (wave < kUserWaveCount && ((gUserMask >> wave) & 1u)) {
        for (int n = 0; n < kWaveFrames; ++n) out[n] = gUserRaw[wave][n];
        return;
    }
    const int16_t* tab = kWaveDataVS + wave * kWaveMips * kWaveFrames;
    float scale = kWaveDataGain / 32767.0f * 2048.0f;
#if PROVS_HAS_PVM
    if (wave < kPvmUserWaveCount) {
        tab = kPvmUserWaves + wave * kWaveMips * kWaveFrames;
        scale = kPvmUserWaveGain / 32767.0f * 2048.0f;
    }
#endif
    for (int n = 0; n < kWaveFrames; ++n) out[n] = int16_t(std::lround(float(tab[n]) * scale));
}

// The firmware's own arithmetic (@0x7272 ff.), so that a wave made here is
// the wave the original would have stored.
void Engine::mixWave(const int* wave, const int* harmonic, float x, float y, int16_t* out) {
    // Joystick position as 16 bit signed (@0xC54C, @0xC54E); u = x + y and
    // w = y - x, saturated, taken as unsigned
    auto q15 = [](float v) { return int(std::lround(clampf(v, -1.0f, 1.0f) * 32767.0f)); };
    auto sat = [](int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); };
    const int jx = q15(x), jy = q15(y);
    const uint32_t u = uint32_t(sat(jx + jy) + 32768), w = uint32_t(sat(jy - jx) + 32768);
    const uint32_t nu = 0xFFFFu - u, nw = 0xFFFFu - w;
    // A = (1-u)w, B = (1-u)(1-w), C = u(1-w), D = the rest; then 15 bit
    uint32_t gain[4];
    gain[0] = (nu * w) >> 16;
    gain[1] = (nw * nu) >> 16;
    gain[2] = (u * nw) >> 16;
    gain[3] = 0xFFFFu - ((gain[0] + gain[1] + gain[2]) & 0xFFFFu);
    for (uint32_t& g : gain) g >>= 1;

    int16_t src[4][kWaveFrames];
    int step[4];
    for (int k = 0; k < 4; ++k) {
        waveSamples(wave[k], src[k]);
        step[k] = harmonic[k] < 1 ? 1 : (harmonic[k] > 32 ? 32 : harmonic[k]);
    }
    int32_t mix[kWaveFrames];
    int32_t peak = 0;
    for (int n = 0; n < kWaveFrames; ++n) {
        int64_t acc = 0;
        for (int k = 0; k < 4; ++k)                           // samples left-aligned in 16 bits
            acc += int64_t(src[k][(n * step[k]) & (kWaveFrames - 1)]) * 16 * int64_t(gain[k]);
        mix[n] = int32_t(((2 * acc) >> 16) + 8);              // @0x7304: rounds to 12 bits
        const int32_t mag = mix[n] < 0 ? -mix[n] - 1 : mix[n];
        if (mag > peak) peak = mag;
    }
    // The ROM's waves fit twelve bits, and so does any mix of them. The
    // reconstructed ones overshoot a little and the Mini's are hotter: a mix
    // of those that would clip is scaled down as a whole instead, which
    // costs a little level and nothing of the timbre.
    for (int n = 0; n < kWaveFrames; ++n) {
        const int32_t v = peak > 32767 ? int32_t(int64_t(mix[n]) * 32767 / peak) : mix[n];
        out[n] = int16_t(v >> 4);
    }
}

void Engine::packWave(const int16_t* samples, uint8_t* record) {
    for (int n = 0; n < kWaveFrames; ++n) record[n] = uint8_t((samples[n] >> 4) & 0xFF);
    for (int n = 0; n < kWaveFrames; n += 2)
        record[kWaveFrames + n / 2] = uint8_t(((samples[n] & 15) << 4) | (samples[n + 1] & 15));
}

void Engine::unpackWave(const uint8_t* record, int16_t* samples) {       // @0x73E4
    for (int n = 0; n < kWaveFrames; ++n) {
        const uint8_t lo = record[kWaveFrames + n / 2];
        samples[n] = int16_t(int(int8_t(record[n])) * 16 + ((n & 1) ? (lo & 15) : (lo >> 4)));
    }
}

// --- Envelopes -----------------------------------------------------------------

// Level and time sequence like the firmware's working tables (@0x479C, @0x47EE):
// segments 0..2 forward (L0->L1->L2->L3), 3..5 backward (L3->L2->L1->L0).
static inline float envPoint(const Env5& p, int i) {
    static const int idx[7] = {0, 1, 2, 3, 2, 1, 0};
    return p.level[idx[i]];
}
static inline float envSegTime(const Env5& p, int seg) {
    static const int idx[6] = {0, 1, 2, 2, 1, 0};
    return p.time[idx[seg]];
}

void Engine::envSegment(EnvState& e, const Env5& p, int stage) const {
    e.stage = stage;
    if (stage == kEnvRelease) {
        // Release: exponentially to L4 (firmware: v = L4 + (v-L4)*k per tick)
        e.target = p.level[4];
        const float tau = (altRelease_ && p.releaseTauAlt >= 0.0f) ? p.releaseTauAlt : p.releaseTau;
        e.coef = tau > 0.0f ? 1.0f - std::exp(-1.0f / (tau * sr_)) : 1.0f;
        e.left = 1;
        return;
    }
    // Each segment starts exactly at its start point (firmware @0x0A48);
    // for loop 1..3 that is the jump back.
    e.v = envPoint(p, stage);
    e.target = envPoint(p, stage + 1);
    int n = int(envSegTime(p, stage) * sr_);
    if (n < 1) n = 1;
    e.left = n;
    e.inc = (e.target - e.v) / float(n);
    e.coef = p.curve > 0.0f ? 1.0f - std::exp(-p.curve / float(n)) : 0.0f;
}

void Engine::envStart(EnvState& e, const Env5& p) const {
    // Repeat counter as at @0x4756: without loop 1, with loop repeat+1, 0 = endless
    e.repeat = p.loop == 0 ? 1 : (p.repeat ? p.repeat + 1 : 0);
    envSegment(e, p, 0);
}

void Engine::envRelease(EnvState& e, const Env5& p) const {
    if (e.stage < kEnvRelease) envSegment(e, p, kEnvRelease);
}

void Engine::envTick(EnvState& e, const Env5& p) const {
    if (e.left <= 0) return;                 // holding or done
    if (e.stage == kEnvRelease) {
        e.v += (e.target - e.v) * e.coef;
        // End as in the firmware: remainder below 1 LSB of the 16-bit envelope
        if (std::fabs(e.target - e.v) < 1.0f / 32768.0f) {
            e.v = e.target; e.left = 0; e.stage = kEnvDone;
        }
        return;
    }
    if (p.curve > 0.0f) e.v += (e.target - e.v) * e.coef;
    else e.v += e.inc;
    if (--e.left > 0) return;
    e.v = e.target;
    const int loop = p.loop > 6 ? 3 : p.loop;
    const int end = loop >= 4 ? 10 - loop : 3;          // first segment not played
    const int next = e.stage + 1;
    if (next < end) { envSegment(e, p, next); return; }
    if (--e.repeat == 0) { e.stage = kEnvHold; return; }
    envSegment(e, p, loop >= 4 ? loop - 4 : loop - 1);
}

// --- Pitch -----------------------------------------------------------------------

// Firmware @0x5D12: cent = (Note-27)*100 + Coarse*100 + Fine, integer.
// Here relative to A4 (note 69) = 440 Hz.
float Engine::oscHz(int note, int osc) const {
    const OscParams& o = p_.osc[osc & 3];
    const float cents = float((note - 69 + p_.transpose) * 100 + o.coarse * 100 + o.fine)
                      + bend_ * p_.bendRange * 100.0f + p_.tuneCents;
    return 440.0f * std::exp2(cents / 1200.0f);
}

// --- Modulation ----------------------------------------------------------------

// Amount 0..99 via the linear table @0x6590 (99 = full scale)
static inline float amt99(int v) {
    if (v > 99) v = 99; else if (v < -99) v = -99;
    return float(v) / 99.0f;
}

// Two global LFOs as in the ISR @0x8F6: 16-bit phase, step per tick from
// kLfoRateStep, output = shape * amount. The second LFO sees the new value
// of the first, the first the old value of the second (order of the firmware).
void Engine::lfoAdvance(int samples, int layer) {
    const Params& P = lp(layer);
    Lfo* L = lfo_[layer];
    const float ticks = float(samples) / (kFwTickSeconds * sr_);
    const float press = pressure_ * amt99(P.pressAmount);
    for (int i = 0; i < 2; ++i) {
        const LfoParams& lp = P.lfo[i];
        const LfoParams& op = P.lfo[1 - i];
        const float other = L[1 - i].out;
        // Rate: table value + other LFO (scale /128), never negative
        float step = lp.hz > 0.0f
            ? lp.hz * 2.0f * 65536.0f * kFwTickSeconds      // triangle: 2 passes per period
            : float(kLfoRateStep[lp.rate > 99 ? 99 : (lp.rate < 0 ? 0 : lp.rate)]);
        if (op.dest & kLfoOtherRate) step += other * 256.0f;
        if (P.pressDest & (i == 0 ? kPressLfo1Rate : kPressLfo2Rate)) step += press * 8192.0f;
        if (step < 0.0f) step = 0.0f;
        // Amount: base value + mod wheel + other LFO (scale /4), 0..1
        float amount = amt99(lp.amount);
        if (P.wheelDest & (i == 0 ? kWheelLfo1 : kWheelLfo2)) amount += wheel_;
        if (op.dest & kLfoOtherAmount) amount += other * 0.25f;
        if (P.pressDest & (i == 0 ? kPressLfo1Amount : kPressLfo2Amount)) amount += press;
        amount = clampf(amount, 0.0f, 1.0f);

        Lfo& l = L[i];
        const bool wraps = lp.shape == kLfoSaw || lp.shape == kLfoRamp;
        if (wraps) l.dir = lp.shape == kLfoSaw ? -1.0f : 1.0f;
        l.phase += l.dir * step * ticks / 32768.0f;
        if (wraps) {
            while (l.phase >= 1.0f) l.phase -= 2.0f;
            while (l.phase < -1.0f) l.phase += 2.0f;
        } else if (l.phase >= 1.0f || l.phase < -1.0f) {
            // reverse at the boundary and draw a new random value
            l.phase = l.phase >= 1.0f ? 2.0f - l.phase : -2.0f - l.phase;
            l.phase = clampf(l.phase, -1.0f, 1.0f);
            l.dir = -l.dir;
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            l.rnd = float(int32_t(rng_)) * (1.0f / 2147483648.0f);
        }
        float shape = l.phase;
        if (lp.shape == kLfoSquare) shape = l.dir;
        else if (lp.shape == kLfoRandom) shape = l.rnd;
        l.out = shape * amount;
    }
}

// Everything the firmware computes per tick for each voice: pitch, cutoff,
// VCA level, pan and the mix offset.
void Engine::voiceControl(Voice& v, bool snap) {
    // The new values are not jumped to but approached linearly until the next
    // control tick (snap = immediately, at note start).
    const float before[10] = {v.fcoef, v.level, v.panL, v.panR, v.modX, v.modY,
                              v.inc[0], v.inc[1], v.inc[2], v.inc[3]};
    const Params& P = lp(v.layer);
    const Lfo* L = lfo_[v.layer];
    const float fenvMod = v.filt.v * amt99(P.fenvAmount);   // -1 .. +1
    // Pressure (@0x3B58): value * amount; full scale on level, pan and
    // LFO amount, /4 on filter, mix and LFO rate, /64 on the pitch
    const float press = pressure_ * amt99(P.pressAmount);

    // Pitch: LFOs +-256 cents (output /128), filter env +-1024 cents (/32)
    float common = bend_ * p_.bendRange * 100.0f + p_.tuneCents + v.detune + v.glide;
    if (P.fenvDest & kFenvFreq) common += fenvMod * 1024.0f;
    for (int i = 0; i < 4; ++i) {
        const OscParams& o = P.osc[i];
        const float osc = float((v.note - 69 + P.transpose) * 100 + o.coarse * 100 + o.fine);
        const float base = osc + bend_ * p_.bendRange * 100.0f + p_.tuneCents;
        float cents = osc + common;
        for (int k = 0; k < 2; ++k)
            if (P.lfo[k].dest & (kLfoFreqA << i)) cents += L[k].out * 256.0f;
        if (P.pressDest & (kPressFreqA << i)) cents += press * 512.0f;
        // Edit Waveform mode reads the wave at a multiple of the pitch (@0x4418)
        const float harm = float(o.harmonic < 1 ? 1 : (o.harmonic > 32 ? 32 : o.harmonic));
        v.inc[i] = 440.0f * std::exp2(cents / 1200.0f) * harm * float(kWaveFrames) / sr_;
        // Mip level chosen by the unmodulated pitch: highest harmonic below
        // half the sample rate, no switching during vibrato
        const float maxHarm = 0.5f * sr_ / (440.0f * std::exp2(base / 1200.0f) * harm);
        int mip = 0;
        while (mip < kWaveMips - 1 && float(kMipHarmonics[mip]) > maxHarm) ++mip;
        const int w = o.wave & 127;
        v.tabScale[i] = kWaveDataGain / 32767.0f;
        if (w >= kWaveCountVS) {
            v.tab[i] = nullptr;              // 127 = noise
        } else if (w < kUserWaveCount && ((gUserMask >> w) & 1u)) {
            // a user wave that has been written
            v.tab[i] = gUserMips[w][mip];
            v.tabScale[i] = 1.0f / kUserUnit;
#if PROVS_HAS_PVM
        } else if (w < kPvmUserWaveCount) {
            // Waves 0..31 are not in the ROM; if available, taken from the Behringer data
            v.tab[i] = kPvmUserWaves + (w * kWaveMips + mip) * kWaveFrames;
            v.tabScale[i] = kPvmUserWaveGain / 32767.0f;
#endif
        } else {
            v.tab[i] = kWaveDataVS + (w * kWaveMips + mip) * kWaveFrames;
        }
    }

    // Filter (ISR @0x9B6): CV = cutoff + key + env*depth + LFOs/4, 0..1.
    // Characteristic CV -> Hz: kVcf constants in engine.h (37 Hz .. 71 kHz,
    // 10.9 octaves).
    float depth = P.envAmount;
    if (P.velDest & kVelFilterEnv) depth = clampf(depth + v.velMod, 0.0f, 1.0f);
    float cv = P.cutoff;
    if (P.keyDest & kKeyFilter) cv = clampf(cv + v.keyMod, 0.0f, 1.0f);
    cv += depth * v.filt.v;
    for (int k = 0; k < 2; ++k)
        if (P.lfo[k].dest & kLfoFilter) cv += L[k].out * 0.25f;
    if (P.pressDest & kPressFilter) cv += press * 0.25f;
    cv = clampf(cv, 0.0f, 1.0f);
    // Smoothing at the frequency input (schematic: 2.2 uF against roughly 1 kOhm = 2.1 ms)
    if (v.vcfCv < 0.0f || snap) v.vcfCv = cv;
    else v.vcfCv += (cv - v.vcfCv) * (1.0f - std::exp(-float(kControlSamples) / (0.0021f * sr_)));
    cv = v.vcfCv;
    float cut = kVcfHzAt0 * std::exp((cv * kVcfSpanMv - kVcfOffsetMv) / kVcfVtMv);
    if (cut > 0.45f * sr_) cut = 0.45f * sr_;
    // Coefficient chosen so that the four poles, together with the one-sample
    // delay of the feedback, turn the phase by 180 degrees at the cutoff frequency:
    // that is then where the resonance sits, even at high frequencies.
    {
        const float w = 6.2831853f * cut / sr_;
        const float t = std::tan(0.78539816f - 0.25f * w);
        const float a = t / (std::sin(w) + t * std::cos(w));
        v.fcoef = clampf(1.0f - a, 0.0001f, 1.0f);
    }

    // VCA level (ISR @0xCCE): base level + velocity, 0..1
    float level = P.ampLevel;
    if (P.velDest & kVelAmp) level += v.velMod;
    level = clampf(level, 0.0f, 1.0f);
    if (P.pressDest & kPressVolume) level = clampf(level + press, 0.0f, 1.0f);
    // [ANN] Unison: 8 voices on one note, level halved for the headroom
    v.level = P.unison > 0 ? 0.5f * level : level;

    // Mix offset (ISR @0xABA): keyboard, velocity, LFOs at full scale
    float mx = 0.0f, my = 0.0f;
    if (P.keyDest & kKeyMixAC) mx += v.keyMod;
    if (P.keyDest & kKeyMixBD) my += v.keyMod;
    if (P.velDest & kVelMixAC) mx += v.velMod;
    if (P.velDest & kVelMixBD) my += v.velMod;
    for (int k = 0; k < 2; ++k) {
        if (P.lfo[k].dest & kLfoMixAC) mx += L[k].out;
        if (P.lfo[k].dest & kLfoMixBD) my += L[k].out;
    }
    if (P.pressDest & kPressMixAC) mx += press * 0.25f;
    if (P.pressDest & kPressMixBD) my += press * 0.25f;
    v.modX = mx;
    v.modY = my;

    // Pan (ISR @0xD86): voice pan + keyboard*2 + velocity + LFOs + env*2;
    // full scale = half the panorama width.
    float pan = p_.pan[v.index & (kMaxVoices - 1)];
    if (P.keyDest & kKeyPan) pan += 2.0f * v.keyMod;
    if (P.velDest & kVelPan) pan += v.velMod;
    if (P.fenvDest & kFenvPan) pan += 2.0f * fenvMod;
    if (P.pressDest & kPressPan) pan += press;
    for (int k = 0; k < 2; ++k)
        if (P.lfo[k].dest & kLfoPan) pan += L[k].out;
    // CEM3379: the two pan VCAs are complementary and linear, each at
    // half gain in the center
    const float pos = clampf(0.5f + 0.5f * pan, 0.0f, 1.0f);
    v.panL = 1.0f - pos;
    v.panR = pos;

    float* cur[10] = {&v.fcoef, &v.level, &v.panL, &v.panR, &v.modX, &v.modY,
                      &v.inc[0], &v.inc[1], &v.inc[2], &v.inc[3]};
    for (int k = 0; k < 10; ++k) {
        if (snap) { v.ramp[k] = 0.0f; continue; }
        v.ramp[k] = (*cur[k] - before[k]) * (1.0f / float(kControlSamples));
        *cur[k] = before[k];
    }
}

// --- Notes ---------------------------------------------------------------------

void Engine::countActive() {
    active_ = 0;
    for (int i = 0; i < kMaxVoices; ++i) active_ += voices_[i].on ? 1 : 0;
}

// Mono voicing of the arpeggiator (@0x5FC6): voice 1 belongs to its first
// sequence, and voice 5 to the second whenever there can be one - a split or
// double program, or the Layer option. Keys played beside it use the others.
bool Engine::arpOwns(int voice) const {
    if (!arpOn_ || p_.arpPoly) return false;
    if (voice == 0) return true;
    return voice == kMaxVoices / 2 && (p_.keyMode != kKeyNormal || p_.arpLayer);
}

int Engine::pickVoice(int lo, int hi) {
    for (int i = lo; i < hi; ++i)
        if (!voices_[i].on && !arpOwns(i)) return i;
    int best = -1;                           // oldest releasing one, otherwise the oldest
    uint64_t oldest = ~0ull;
    for (int i = lo; i < hi; ++i)
        if (!arpOwns(i) && voices_[i].amp.stage >= kEnvRelease && voices_[i].age < oldest) {
            oldest = voices_[i].age; best = i;
        }
    if (best < 0)
        for (int i = lo; i < hi; ++i)
            if (!arpOwns(i) && voices_[i].age < oldest) { oldest = voices_[i].age; best = i; }
    return best < 0 ? lo : best;
}

void Engine::startVoice(int index, int note, int velocity, float detune, int layer) {
    const Params& P = lp(layer);
    Voice& v = voices_[index];
    // Glide (@0x5AB6): the voice glides from its most recently played
    // (momentary) pitch to the new one
    const float target = float(note * 100) + detune;
    const float from = lastCents_[index] + (v.on ? v.glide : 0.0f);
    const bool glides = P.glide > 0 && played_[index];
    v = Voice{};
    v.on = true;
    v.held = true;
    v.index = index;
    v.layer = layer;
    v.arpSeq = arpSeqNow_;
    v.note = note;
    v.detune = detune;
    v.glide = glides ? from - target : 0.0f;
    lastCents_[index] = target;
    played_[index] = true;
    v.age = ++clock_;
    // Velocity (@0x5AE4): curve @0x6690 times amount; keyboard (@0x5B0C):
    // (note-66) * amount / 64 of full scale
    const int vel = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
    v.velMod = float(kVelocityCurve[vel]) / 32767.0f * amt99(P.velAmount);
    v.keyMod = float(note - 66) * amt99(P.keyAmount) / 64.0f;
    envStart(v.amp, P.ampEnv);
    envStart(v.filt, P.filterEnv);
    envStart(v.mx, P.mixEnvX);
    envStart(v.my, P.mixEnvY);
    voiceControl(v, true);
}

// Voice range of a layer (@0x120A): normally all 8, with split/double
// main layer 1-4 and linked program 5-8.
void Engine::layerRange(int layer, int& lo, int& hi) const {
    if (p_.keyMode == kKeyNormal) { lo = 0; hi = kMaxVoices; }
    else if (layer == 0) { lo = 0; hi = kMaxVoices / 2; }
    else { lo = kMaxVoices / 2; hi = kMaxVoices; }
}

// Unison (@0x1284, @0x404E): all voices of the layer play the lowest
// held key, voice k higher by k * detune cents; with split/double and
// four voices the step is twice as large (@0x4078).
void Engine::unisonUpdate(int layer, int velocity) {
    int lo, hi;
    layerRange(layer, lo, hi);
    int lowest = -1;
    for (int i = 0; i < heldCount_[layer]; ++i)
        if (lowest < 0 || heldNotes_[layer][i] < lowest) lowest = heldNotes_[layer][i];
    if (lowest == unisonNote_[layer]) return;
    unisonNote_[layer] = lowest;
    if (lowest < 0) {
        for (int i = lo; i < hi; ++i) {
            Voice& v = voices_[i];
            if (!v.on || !v.held || arpOwns(i)) continue;
            if (hold_) v.pending = true; else startRelease(v);
        }
        return;
    }
    const int step = lp(layer).unison * (p_.keyMode == kKeyNormal ? 1 : 2);
    const float base = (layer == 1 && p_.keyMode == kKeyDouble) ? float(p_.doubleDetune) : 0.0f;
    for (int i = lo; i < hi; ++i)
        if (!arpOwns(i)) startVoice(i, lowest, velocity, base + float((i - lo) * step), layer);
}

void Engine::layerNoteOn(int layer, int note, int velocity) {
    if (lp(layer).unison > 0) {
        if (heldCount_[layer] < 16) heldNotes_[layer][heldCount_[layer]++] = note;
        unisonUpdate(layer, velocity);
        return;
    }
    int lo, hi;
    layerRange(layer, lo, hi);
    // Double: the linked program sounds doubleDetune cents higher (@0x4064)
    const float detune = (layer == 1 && p_.keyMode == kKeyDouble) ? float(p_.doubleDetune) : 0.0f;
    startVoice(pickVoice(lo, hi), note, velocity, detune, layer);
}

void Engine::layerNoteOff(int layer, int note) {
    if (lp(layer).unison > 0) {
        int& n = heldCount_[layer];
        for (int i = 0; i < n; ++i)
            if (heldNotes_[layer][i] == note) { heldNotes_[layer][i] = heldNotes_[layer][--n]; break; }
        unisonUpdate(layer, 100);
        return;
    }
    int lo, hi;
    layerRange(layer, lo, hi);
    for (int i = lo; i < hi; ++i) {
        Voice& v = voices_[i];
        if (!v.on || !v.held || v.note != note || v.layer != layer) continue;
        if ((v.arpSeq >= 0) != (arpSeqNow_ >= 0)) continue;
        if (hold_) v.pending = true; else startRelease(v);
    }
}

void Engine::queueDelayed(int note, int velocity, bool on) {
    if (delayedCount_ >= 32) return;
    delayed_[delayedCount_++] = {int(p_.doubleDelay * sr_), note, velocity, on};
}

void Engine::keyOn(int note, int velocity) {
    if (paramsDirty_) applyParams();
    if (p_.keyMode == kKeySplit) {
        // @0x126E: from the split point upward the main layer, below it the linked program
        layerNoteOn(note >= p_.splitPoint ? 0 : 1, note, velocity);
    } else if (p_.keyMode == kKeyDouble) {
        layerNoteOn(0, note, velocity);
        if (p_.doubleDelay > 0.0f) {
            // @0x5458: delayed note with the velocity reduced by 3
            const int v2 = velocity > 3 ? velocity - 3 : 1;
            queueDelayed(note, v2, true);
        } else {
            layerNoteOn(1, note, velocity);
        }
    } else {
        layerNoteOn(0, note, velocity);
    }
    countActive();
}

void Engine::startRelease(Voice& v) {
    v.held = false;
    v.pending = false;
    const Params& P = lp(v.layer);
    envRelease(v.amp, P.ampEnv);
    envRelease(v.filt, P.filterEnv);
    envRelease(v.mx, P.mixEnvX);
    envRelease(v.my, P.mixEnvY);
}

void Engine::keyOff(int note) {
    if (p_.keyMode == kKeyNormal) { layerNoteOff(0, note); return; }
    layerNoteOff(0, note);
    if (p_.keyMode == kKeyDouble && p_.doubleDelay > 0.0f) queueDelayed(note, 0, false);
    else layerNoteOff(1, note);
}

// --- Arpeggiator ---------------------------------------------------------------
// Sequence as in the firmware (@0x5FEC ff.): the internal clock has a period of
// (106 - rate) interrupts of 1 ms each (@0x5F26). One step per clocks/step; the
// note ends after half of the step (@0x6020).
//
// There are two sequences (@0xFFFFB828 and @0xFFFFBA26), stepped together
// (@0x607A). The first takes the keys. The second holds the overdub of the
// Layer option, or the keys left of the split point when the arpeggiator
// split is set to dual.

void Engine::arpSeqReset(ArpSeq& q) {            // @0x6532
    q.count = 0;
    q.mode = p_.arpMode;
    q.latched = false;
    q.pos = -1;
    q.oct = 0;
    q.dir = 1;
    q.repeatLeft = 0;
}

void Engine::setArp(bool on) {
    if (on == arpOn_) return;
    if (paramsDirty_) applyParams();
    if (!on) { arpGateOff(0); arpGateOff(1); }
    arpOn_ = on;
    arpSeqReset(arp_[0]);
    arpSeqReset(arp_[1]);
    arpLatched_ = false;
    arpArmed_ = on && arpLatchWanted_;
    arpClock_ = 0;
    arpSamples_ = 0.0f;
}

// Ends the note a sequence sounds. In poly voicing that is a key release like
// any other; whatever the sequence started and is still held goes with it, so
// a change of voicing in between leaves nothing hanging.
void Engine::arpGateOff(int s) {
    ArpSeq& q = arp_[s];
    if (!q.sounding) return;
    q.sounding = false;
    arpSeqNow_ = s;
    if (p_.arpPoly) keyOff(q.note);
    for (int i = 0; i < kMaxVoices; ++i) {
        Voice& v = voices_[i];
        if (!v.on || !v.held || v.arpSeq != s) continue;
        if (hold_) v.pending = true; else startRelease(v);
    }
    arpSeqNow_ = -1;
}

void Engine::arpClock() {
    if (--arpClock_ <= 0) {
        arpClock_ = arpClocksPerStep_;
        arpStepSeq(0);
        arpStepSeq(1);
    } else if (arpClock_ == arpClocksPerStep_ / 2) {
        arpGateOff(0);
        arpGateOff(1);
    }
}

void Engine::arpStepSeq(int s) {
    ArpSeq& q = arp_[s];
    arpGateOff(s);
    const int n = q.count;
    if (n <= 0) return;
    const int octs = p_.arpOctaves < 0 ? 0 : (p_.arpOctaves > 3 ? 3 : p_.arpOctaves);

    if (q.repeatLeft > 0 && q.pos >= 0 && q.pos < n) {
        --q.repeatLeft;                      // the same note once more
    } else {
        q.repeatLeft = p_.arpRepeats < 0 ? 0 : (p_.arpRepeats > 3 ? 3 : p_.arpRepeats);
        const bool first = q.pos < 0 || q.pos >= n;
        switch (p_.arpScan) {
            case kArpDown:
                if (first) { q.pos = n - 1; q.oct = octs; }
                else if (--q.pos < 0) { q.pos = n - 1; if (--q.oct < 0) q.oct = octs; }
                break;
            case kArpUpDown:
                // reverse at the boundary without repeating the boundary note (@0x61C0)
                if (first) { q.pos = 0; q.oct = 0; q.dir = 1; break; }
                if (n == 1 && octs == 0) break;
                if (q.dir > 0) {
                    if (q.pos + 1 < n) ++q.pos;
                    else if (q.oct < octs) { q.pos = 0; ++q.oct; }
                    else { q.dir = -1; if (--q.pos < 0) { q.pos = n - 1; --q.oct; } }
                } else {
                    if (q.pos > 0) --q.pos;
                    else if (q.oct > 0) { q.pos = n - 1; --q.oct; }
                    else { q.dir = 1; if (++q.pos >= n) { q.pos = 0; ++q.oct; } }
                }
                break;
            case kArpRandom:
                rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                q.pos = int((rng_ >> 8) % uint32_t(n));
                q.oct = int((rng_ >> 20) % uint32_t(octs + 1));
                break;
            case kArpReverse:
                if (first) { q.pos = n - 1; q.oct = 0; }
                else if (--q.pos < 0) { q.pos = n - 1; if (++q.oct > octs) q.oct = 0; }
                break;
            default:   // Up, Assign
                if (first) { q.pos = 0; q.oct = 0; }
                else if (++q.pos >= n) { q.pos = 0; if (++q.oct > octs) q.oct = 0; }
                break;
        }
    }
    if (q.oct > octs) q.oct = octs;
    if (q.oct < 0) q.oct = 0;

    // Up/Down/UpDown run in pitch order, Assign/Reverse/Random in the
    // order in which the keys were struck
    int index = q.pos;
    if (p_.arpScan <= kArpUpDown) {
        int order[kArpMaxKeys];
        for (int i = 0; i < n; ++i) order[i] = i;
        for (int i = 1; i < n; ++i) {
            const int k = order[i];
            int j = i - 1;
            while (j >= 0 && q.keys[order[j]].note > q.keys[k].note) { order[j + 1] = order[j]; --j; }
            order[j + 1] = k;
        }
        index = order[q.pos];
    }
    const ArpKey key = q.keys[index];
    // Rests (@0x6464): the top C of the VS keyboard stands for a rest, and
    // with an arpeggiator split the lowest C as well - the rest key of the
    // left half (owner's manual ch. 5)
    if (p_.arpRests && (key.note >= 96 || (p_.arpSplit != 0 && key.note <= 36))) return;
    // Pitch as at @0x62BC: key + 12 * octave + transpose, brought back into
    // range by octaves
    int note = key.note + 12 * q.oct + arpTranspose_;
    while (note < 0) note += 12;
    while (note > 127) note -= 12;
    const int vel = p_.arpVelocity ? key.velocity : 80;
    q.note = note;
    q.sounding = true;
    arpSeqNow_ = s;
    if (p_.arpPoly) {
        keyOn(note, vel);                    // like a played key (@0x62EE)
    } else {
        // Mono voicing (@0x6306): the first sequence plays voice 1 and the
        // second voice 5 - with split or double that is the linked program,
        // in double detuned like it. The voice is restarted on every note.
        const int voice = s ? kMaxVoices / 2 : 0;
        const int layer = (s && p_.keyMode != kKeyNormal) ? 1 : 0;
        const float detune = (s && p_.keyMode == kKeyDouble) ? float(p_.doubleDetune) : 0.0f;
        startVoice(voice, note, vel, detune, layer);
        countActive();
    }
    arpSeqNow_ = -1;
}

int Engine::keysDown(int lo, int hi) const {
    int n = 0;
    for (int k = lo < 0 ? 0 : lo; k < hi && k < 128; ++k) n += (keyDown_[k >> 5] >> (k & 31)) & 1u;
    return n;
}

// Where a key goes (@0x6326): -1 = it is played normally, 0 = into the first
// sequence, 1 = into the second.
int Engine::arpRoute(int note) const {
    if (!arpOn_) return -1;
    const bool split = p_.keyMode == kKeySplit && p_.arpSplit != 0;
    const bool left = note < p_.splitPoint;
    if (!arpLatched_) {                              // @0x633E
        if (!split) return 0;
        if (!left) return (p_.arpSplit & 1) ? 0 : -1;          // right: Right and Dual
        return p_.arpSplit == 2 ? 0 : (p_.arpSplit == 3 ? 1 : -1);
    }
    if (!p_.arpLayer) return -1;                     // @0x6374: latched, no overdub
    if (!split) return 1;
    // Dual has both sequences in use; a side can be overdubbed only if it was
    // empty when the latch was set (flag at 0x1F0)
    if (!left) {
        if (p_.arpSplit == 1) return 1;
        return (p_.arpSplit == 3 && !arp_[0].latched) ? 0 : -1;
    }
    if (p_.arpSplit == 2) return 1;
    return (p_.arpSplit == 3 && !arp_[1].latched) ? 1 : -1;
}

void Engine::arpLatchNow() {                         // @0x2FCE
    arpLatched_ = true;
    arpArmed_ = false;
    for (ArpSeq& q : arp_)
        if (q.count > 0) q.latched = true;
}

void Engine::setArpLatch(bool on) {
    arpLatchWanted_ = on;
    if (!arpOn_) return;
    if (paramsDirty_) applyParams();
    if (on) {
        if (arpLatched_) return;
        if (arpAny()) arpLatchNow(); else arpArmed_ = true;
        return;
    }
    arpLatched_ = arpArmed_ = false;
    const bool dual = p_.keyMode == kKeySplit && p_.arpSplit == 3;
    for (int s = 0; s < 2; ++s) {
        ArpSeq& q = arp_[s];
        q.latched = false;
        if (s == 1 && !dual) {                       // an overdub goes with the latch
            arpGateOff(1);
            q.count = 0;
            q.pos = -1;
            continue;
        }
        if (p_.arpMode != kArpNormal) continue;      // those modes remember by themselves
        int kept = 0;
        for (int i = 0; i < q.count; ++i)
            if ((keyDown_[q.keys[i].note >> 5] >> (q.keys[i].note & 31)) & 1u) q.keys[kept++] = q.keys[i];
        q.count = kept;
        if (q.pos >= kept) q.pos = -1;
        if (kept == 0) arpGateOff(s);
    }
}

// Chord mode of the Pro VS Mini (FUN_2400a694): key + table[set][pitch class]
#if PROVS_HAS_PVM
static const unsigned char* chordOf(int set, int note) {
    if (set < 1 || set > kChordSetCount) return nullptr;
    return kChordSets[set - 1][((note % 12) + 12) % 12];
}
#else
static const unsigned char* chordOf(int, int) { return nullptr; }
#endif

void Engine::noteOn(int note, int velocity) {
    if (paramsDirty_) applyParams();
    const unsigned char* c = chordOf(p_.chordSet, note);
    if (!c) { inputOn(note, velocity); return; }
    for (int k = 0; k < 3; ++k)
        if (k == 0 || c[k] != c[k - 1]) inputOn(note + c[k], velocity);
}

void Engine::noteOff(int note) {
    const unsigned char* c = chordOf(p_.chordSet, note);
    if (!c) { inputOff(note); return; }
    for (int k = 0; k < 3; ++k)
        if (k == 0 || c[k] != c[k - 1]) inputOff(note + c[k]);
}

void Engine::inputOn(int note, int velocity) {
    if (note < 0 || note > 127) return;
    if (paramsDirty_) applyParams();
    const int s = arpRoute(note);
    // Auto-latch (@0x64B0) asks whether another key is down - on this side of
    // the split point if the arpeggiator is split, anywhere otherwise
    const bool split = p_.keyMode == kKeySplit && p_.arpSplit != 0;
    const int others = !split ? keysDown(0, 128)
                     : (note < p_.splitPoint ? keysDown(0, p_.splitPoint) : keysDown(p_.splitPoint, 128));
    keyDown_[note >> 5] |= 1u << (note & 31);
    if (s < 0) { keyOn(note, velocity); return; }

    ArpSeq& q = arp_[s];
    if (q.mode != p_.arpMode) arpSeqReset(q);        // @0x63BE: recorded in another mode
    // Auto-latch: a new key after all have been released starts over
    if (p_.arpMode == kArpAutoLatch && others == 0) arpSeqReset(q);
    const bool wasEmpty = q.count == 0;
    bool present = false;
    if (p_.arpMode != kArpExtend)
        for (int i = 0; i < q.count; ++i) present |= q.keys[i].note == note;
    if (!present && q.count < kArpMaxKeys) q.keys[q.count++] = {note, velocity};
    if (wasEmpty && q.count > 0) {                   // @0x64F6: the first key restarts the clock
        q.pos = -1;
        q.oct = 0;
        q.dir = 1;
        q.repeatLeft = 0;
        arpClock_ = 0;
        arpSamples_ = 0.0f;
    }
}

void Engine::inputOff(int note) {
    if (note < 0 || note > 127) return;
    keyDown_[note >> 5] &= ~(1u << (note & 31));
    const int s = arpRoute(note);
    bool found = false;
    if (s >= 0) {
        ArpSeq& q = arp_[s];
        for (int i = 0; i < q.count && !found; ++i) {
            if (q.keys[i].note != note) continue;
            found = true;
            // @0x63F6: only the normal mode forgets a released key - and not
            // while the latch is waiting for the chord to be complete
            if (p_.arpMode != kArpNormal || arpArmed_) break;
            for (int k = i + 1; k < q.count; ++k) q.keys[k - 1] = q.keys[k];
            --q.count;
            if (q.pos >= i && q.pos >= 0) --q.pos;
            if (q.count == 0) { arpGateOff(s); q.pos = -1; }
        }
    }
    // Not a key of a sequence: it was played normally, perhaps before the
    // arpeggiator was switched on or the latch was set
    if (!found) keyOff(note);
    if (arpArmed_ && keysDown(0, 128) == 0 && arpAny()) arpLatchNow();
}

void Engine::releaseHeld() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices_[i].on && voices_[i].pending) startRelease(voices_[i]);
}

void Engine::allNotesOff() {
    for (int i = 0; i < kMaxVoices; ++i) voices_[i] = Voice{};
    heldCount_[0] = heldCount_[1] = 0;
    unisonNote_[0] = unisonNote_[1] = -1;
    delayedCount_ = 0;
    for (ArpSeq& q : arp_) {
        arpSeqReset(q);
        q.sounding = false;
    }
    for (uint32_t& w : keyDown_) w = 0;
    arpLatched_ = false;
    arpArmed_ = arpOn_ && arpLatchWanted_;       // the latch waits for new keys
    active_ = 0;
}

void Engine::pitchBend(float amount) { bend_ = clampf(amount, -1.0f, 1.0f); }

void Engine::setModWheel(float amount) { wheel_ = clampf(amount, 0.0f, 1.0f); }

// 7-bit value of the firmware: (v<<8 | v) >> 1, i.e. 127 -> 32639 of 32767
void Engine::setPressure(float amount) {
    pressure_ = clampf(amount, 0.0f, 1.0f) * (32639.0f / 32767.0f);
}

// --- Audio ---------------------------------------------------------------------

PROVS_HOT void Engine::render(float* out, int frames) {
    if (paramsDirty_) {
        applyParams();
        ctl_ = 0;                            // recompute immediately
    }
    // Chorus (ISR @0xDD2): depth = base value + pressure + mod wheel,
    // rate = base value + pressure/4 (in phase steps of the LFO table)
    {
        const float press = pressure_ * amt99(p_.pressAmount);
        float mod = 0.0f;
        if (p_.wheelDest & kWheelChorus) mod += wheel_;
        if (p_.pressDest & kPressChorusDepth) mod += press;
        const float rateMod = (p_.pressDest & kPressChorusRate)
            ? press * 8192.0f / (2.0f * 65536.0f * kFwTickSeconds) : 0.0f;
        const float hz = p_.fxRateHz > 0.0f ? p_.fxRateHz + rateMod : 0.0f;
        fx_.setType(p_.fxType);
        fx_.setRate(p_.fxRate);
        fx_.setRateHz(hz > 0.0f ? hz : 0.0f);
        fx_.setDepth(clampf(p_.fxDepth + mod, 0.0f, 1.0f));
        // Original chorus: on the right the selected program, on the left the
        // linked one with split/double, otherwise the selected one as well
        // (owner's manual ch. 12)
        const Params& pl = p_.keyMode != kKeyNormal ? p2_ : p_;
        const float hzR = p_.fxRateHz > 0.0f ? p_.fxRateHz + rateMod : 0.05f + p_.fxRate * 1.15f;
        const float hzL = pl.fxRateHz > 0.0f ? pl.fxRateHz + rateMod : 0.05f + pl.fxRate * 1.15f;
        // Latch U320: D8 = right, D9 = left chorus (schematic SD2400-3)
        fx_.setVsChorus(0, (pl.chorusOn & 2) != 0, hzL, clampf(pl.fxDepth + mod, 0.0f, 1.0f));
        fx_.setVsChorus(1, (p_.chorusOn & 1) != 0, hzR, clampf(p_.fxDepth + mod, 0.0f, 1.0f));
    }

    const float vcaFall = 1.0f - std::exp(-1.0f / (0.0024f * sr_));
    const float fwrap = float(kWaveFrames);

    for (int n = 0; n < frames; ++n) {
        if (ctl_ <= 0) {
            ctl_ = kControlSamples;
            // Arpeggiator clock
            if (arpOn_ && !arpExternal_ && arpAny() && p_.arpRate > 0) {
                arpSamples_ -= float(kControlSamples);
                while (arpSamples_ <= 0.0f) {
                    const int rate = p_.arpRate > 99 ? 99 : p_.arpRate;
                    arpSamples_ += float(106 - rate) * kFwInterruptSeconds * sr_;
                    arpClock();
                }
            }
            lfoAdvance(kControlSamples, 0);
            if (p_.keyMode != kKeyNormal) lfoAdvance(kControlSamples, 1);
            // delayed notes of the second layer (double with delay)
            for (int d = 0; d < delayedCount_;) {
                Delayed& e = delayed_[d];
                e.samples -= kControlSamples;
                if (e.samples > 0) { ++d; continue; }
                const Delayed ev = e;
                for (int k = d + 1; k < delayedCount_; ++k) delayed_[k - 1] = delayed_[k];
                --delayedCount_;
                if (ev.on) layerNoteOn(1, ev.note, ev.velocity);
                else layerNoteOff(1, ev.note);
            }
            for (int vi = 0; vi < kMaxVoices; ++vi) {
                Voice& v = voices_[vi];
                if (!v.on) continue;
                // Glide: toward the target pitch at a fixed speed (cents per tick)
                const int g = lp(v.layer).glide;
                const float gstep = g > 0
                    ? float(kGlideRate[g > 99 ? 99 : g]) / 64.0f
                      * float(kControlSamples) / (kFwTickSeconds * sr_)
                    : 0.0f;
                if (v.glide > gstep) v.glide -= gstep;
                else if (v.glide < -gstep) v.glide += gstep;
                else v.glide = 0.0f;
                voiceControl(v);
            }
        }
        --ctl_;

        float mixL = 0.0f, mixR = 0.0f;
        for (int vi = 0; vi < kMaxVoices; ++vi) {
            Voice& v = voices_[vi];
            if (!v.on) continue;

            const Params& P = lp(v.layer);
            envTick(v.amp, P.ampEnv);
            envTick(v.filt, P.filterEnv);
            envTick(v.mx, P.mixEnvX);
            envTick(v.my, P.mixEnvY);
            if (v.amp.stage == kEnvDone) { v.on = false; continue; }

            // Vector mix as in the firmware (@0xB4E): u = (x+y+1)/2, v = (y-x+1)/2,
            // A = (1-u)v, B = (1-u)(1-v), C = u(1-v), D = uv. In the center 25 % each.
            const float x = clampf(v.mx.v * P.mixAmount + p_.mixX + v.modX, -1.0f, 1.0f);
            const float y = clampf(v.my.v * P.mixAmount + p_.mixY + v.modY, -1.0f, 1.0f);
            const float u = clampf(0.5f * (x + y) + 0.5f, 0.0f, 1.0f);
            const float w = clampf(0.5f * (y - x) + 0.5f, 0.0f, 1.0f);
            const float gains[4] = {
                (1.0f - u) * w,             // A
                (1.0f - u) * (1.0f - w),    // B
                u * (1.0f - w),             // C
                u * w};                     // D

            float sum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                float s;
                if (v.tab[i]) {
                    const int i0 = int(v.phase[i]) & (kWaveFrames - 1);
                    const int i1 = (i0 + 1) & (kWaveFrames - 1);
                    const float fr = v.phase[i] - float(int(v.phase[i]));
                    const float a = float(v.tab[i][i0]);
                    s = (a + (float(v.tab[i][i1]) - a) * fr) * v.tabScale[i];
                } else {
                    s = v.noise[i];
                }
                sum += s * gains[i] * P.osc[i].level;
                const int before = int(v.phase[i]);
                v.phase[i] += v.inc[i];
                if (!v.tab[i] && int(v.phase[i]) != before) {
                    // Noise: new random value per wave sample
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    v.noise[i] = float(int32_t(rng_)) * (1.0f / 2147483648.0f);
                }
                while (v.phase[i] >= fwrap) v.phase[i] -= fwrap;
            }

            // 4-pole low-pass, negative feedback across all poles (resonance), soft-clipped
            const float f = v.fcoef;
            // Owner's manual ch. 7: self-oscillation from about resonance 70. The
            // cascade oscillates at a loop gain of 4.
            const float reso = clampf(P.resonance * (4.0f * 99.0f / 70.0f), 0.0f, 5.0f);
            float fb = clampf(reso * v.f3, -1.2f, 1.2f);
            fb = fb * (1.0f - fb * fb * 0.15f);
            // Data sheet: at full resonance the passband level drops by only 6 dB
            float in = sum * (1.0f + 0.375f * reso) - fb;
            in = clampf(in, -1.5f, 1.5f);
            in = in * (1.0f - in * in * 0.05f);
            v.f0 += f * (in - v.f0);
            v.f1 += f * (v.f0 - v.f1);
            v.f2 += f * (v.f1 - v.f2);
            v.f3 += f * (v.f2 - v.f3);

            const float dc = v.f3 - v.dcX + 0.998f * v.dcY;
            v.dcX = v.f3;
            v.dcY = dc;
            // 0.45: room for several voices plus the chorus share below the sum limiter
            // VCA of the CEM3379: linear in the control voltage, below that (4 % of the
            // scale, -20 dB) a soft roll-off toward zero
            // Control voltage of the VCA (schematic): rises practically instantly via a
            // diode and 390 ohms, falls via 24 kOhm into 0.1 uF (2.4 ms)
            float cvA = v.amp.v * v.level;
            if (cvA < v.vcaCv) { v.vcaCv += (cvA - v.vcaCv) * vcaFall; cvA = v.vcaCv; }
            else v.vcaCv = cvA;
            float vca;
            if (cvA >= 0.04f) vca = 0.1f + (cvA - 0.04f) * (0.9f / 0.96f);
            else { const float t = cvA * 25.0f; vca = 0.1f * t * t * t; }
            const float o = dc * vca * 0.45f * p_.gain;
            mixL += o * v.panL;
            mixR += o * v.panR;
            v.fcoef += v.ramp[0]; v.level += v.ramp[1];
            v.panL += v.ramp[2];  v.panR += v.ramp[3];
            v.modX += v.ramp[4];  v.modY += v.ramp[5];
            v.inc[0] += v.ramp[6]; v.inc[1] += v.ramp[7];
            v.inc[2] += v.ramp[8]; v.inc[3] += v.ramp[9];
        }
        float l = mixL * 2.0f, r = mixR * 2.0f;      // pan center = 0 dB
        fx_.process(l, r);
        // soft sum limiting after the effects (cubic, reaches
        // +-1 at +-1.5)
        l = clampf(l, -1.5f, 1.5f);
        r = clampf(r, -1.5f, 1.5f);
        l -= l * l * l * (1.0f / 6.75f);
        r -= r * r * r * (1.0f / 6.75f);
        out[2 * n] = l;
        out[2 * n + 1] = r;
    }
    countActive();
}

}  // namespace provs
