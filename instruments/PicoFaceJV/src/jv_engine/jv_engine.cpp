// PicoFaceJV sound engine. See jv_engine.h for the contract and
// tools/jv_extract/README.md for where the numbers come from.
#include "jv_engine/jv_engine.h"

#include <math.h>
#include <string.h>

#include "jv_calibration.h"
#include "jv_tone_map.h"

namespace jv {
namespace {

inline uint32_t be24(const uint8_t* p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

// Calibration tables are sampled every 4 parameter steps; interpolate between.
float lookup(const float* tbl, int n, int v) {
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    float x = v * 0.25f;
    int i = (int)x;
    if (i >= n - 1) return tbl[n - 1];
    return tbl[i] + (tbl[i + 1] - tbl[i]) * (x - (float)i);
}

float envRiseSeconds(int v) { return lookup(JV_ENV_RISE_S, JV_ENV_RISE_N, v); }
float envFallSeconds(int v) { return lookup(JV_ENV_FALL_S, JV_ENV_FALL_N, v); }

// A TVA envelope target, on its own measured curve rather than the tone-level
// one -- the two are up to 9.4 dB apart.
float envLevelToLinear(int v) {
    if (v <= 0) return 0.0f;
    const float db = lookup(JV_TVA_ENV_LEVEL_DB, 33, v);
    if (db <= -200.0f) return 0.0f;
    return powf(10.0f, db * (1.0f / 20.0f));
}

// A TVF envelope target scales the cutoff excursion and is near-linear.
float tvfEnvLevel(int v) {
    if (v <= 0) return 0.0f;
    if (v > 127) v = 127;
    const float x = v * (1.0f / 16.0f);
    const int i = (int)x;
    if (i >= 8) return JV_TVF_ENV_LEVEL[8];
    return JV_TVF_ENV_LEVEL[i] +
           (JV_TVF_ENV_LEVEL[i + 1] - JV_TVF_ENV_LEVEL[i]) * (x - (float)i);
}

// The patch-common level has its own curve again, between the other two.
float patchLevelToLinear(int v) {
    if (v <= 0) return 0.0f;
    if (v > 127) v = 127;
    const float x = v * (1.0f / 16.0f);
    const int i = (int)x;
    const float db = (i >= 8) ? JV_PATCH_LEVEL_DB[8]
                              : JV_PATCH_LEVEL_DB[i] +
                                (JV_PATCH_LEVEL_DB[i + 1] - JV_PATCH_LEVEL_DB[i]) * (x - (float)i);
    if (db <= -200.0f) return 0.0f;
    return powf(10.0f, db * (1.0f / 20.0f));
}

// Tone levels follow the measured tvaLevel law.
float levelToLinear(int v) {
    if (v <= 0) return 0.0f;
    float db = lookup(JV_TVA_LEVEL_DB, 32, v);
    if (db <= -200.0f) return 0.0f;
    return powf(10.0f, db * (1.0f / 20.0f));
}

float cutoffToHz(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 127.0f) v = 127.0f;
    // Interpolate the measured table where it exists; above v~84 the corner left
    // the excitation's bandwidth during calibration, so fall back to the fit.
    float x = v * 0.25f;
    int i = (int)x;
    if (i + 1 < 32) {
        float a = JV_TVF_CUTOFF_HZ[i], b = JV_TVF_CUTOFF_HZ[i + 1];
        if (a == a && b == b) return a + (b - a) * (x - (float)i);
    }
    return 431.3f * powf(2.0f, v / 17.93f);
}

// Roland stores several parameters bipolar with the swing at 64.
inline int bipolar(uint8_t v) { return (int)v - 64; }

// Level modulation is the one destination that is not linear in sensitivity.
float modLevelDb(float sens) {
    float a = fabsf(sens);
    if (a <= JV_MOD_LEVEL_SENS[0]) return JV_MOD_LEVEL_DB[0] * a / JV_MOD_LEVEL_SENS[0];
    for (int i = 0; i < 4; i++) {
        if (a <= JV_MOD_LEVEL_SENS[i + 1]) {
            float t = (a - JV_MOD_LEVEL_SENS[i]) /
                      (JV_MOD_LEVEL_SENS[i + 1] - JV_MOD_LEVEL_SENS[i]);
            return JV_MOD_LEVEL_DB[i] + (JV_MOD_LEVEL_DB[i + 1] - JV_MOD_LEVEL_DB[i]) * t;
        }
    }
    return JV_MOD_LEVEL_DB[4];
}

// LFO depth tables are sampled every 8 units, with the last entry at 63 rather
// than 64, so the top interval spans 7 units. Values above 63 disable the
// modulation outright -- measured, they reproduce the unmodulated signal exactly.
// Depth is SIGNED: the sign flips the direction of the modulation, it does not
// disable it. Values 64..127 really are inert, but 128..255 are -128..-1 and
// modulate the other way -- measured on pitch, where -20 gives 133 cents of
// swing around a mean 11 Hz ABOVE the carrier while +20 gives 136 cents around
// a mean 10 Hz below. An earlier sweep only covered 0..127 and concluded that
// everything from 64 up was off.
float lfoDepth(const float* tbl, uint8_t raw) {
    const int sv = (int8_t)raw;
    int d = sv < 0 ? -sv : sv;
    if (d > JV_LFO_DEPTH_MAX) return 0.0f;      // 64..127 is genuinely inert
    float mag;
    if (d >= 56) mag = tbl[7] + (tbl[8] - tbl[7]) * ((float)(d - 56) / 7.0f);
    else {
        float x = d * 0.125f;
        int i = (int)x;
        mag = tbl[i] + (tbl[i + 1] - tbl[i]) * (x - (float)i);
    }
    return sv < 0 ? -mag : mag;
}

// The machine sits a fixed 9.4 cents below equal temperament: measured constant
// to within 1 cent across 2.5 octaves, and it is exactly the residual left over
// when the sample tune words are referenced to their neutral value of 1024.
constexpr float kMasterTuneCents = -9.4f;

} // namespace

// ------------------------------------------------------------------ envelope

void Engine::Env::begin(const uint8_t* t, const uint8_t* l, uint32_t sampleRate,
                        bool logLev) {
    times = t;
    levels = l;
    sr = sampleRate;
    logLevels = logLev;
    level = 0.0f;
    target = 0.0f;
    slope = 0.0f;
    decay = 1.0f;
    stage = 0;
    rising = true;
    remaining = 0.0f;
    segmentValid = false;
}

void Engine::Env::release() {
    if (stage >= 4) return;
    stage = 4;
    segmentValid = false;
}

float Engine::Env::tick() {
    if (stage >= 5) return 0.0f;
    if (stage == 3) return level;   // sustain until note-off

    if (!segmentValid) {
        const int tv = times[stage < 3 ? stage : 3];
        target = (stage == 4) ? 0.0f
                              : (logLevels ? envLevelToLinear(levels[stage])
                                           : tvfEnvLevel(levels[stage]));
        rising = target > level;
        const float secs = rising ? envRiseSeconds(tv) : envFallSeconds(tv);
        remaining = secs * (float)sr;
        if (remaining < 1.0f) remaining = 1.0f;
        if (rising) {
            slope = (target - level) / remaining;          // linear in amplitude
        } else if (target > 1e-6f && level > 1e-6f) {
            decay = powf(target / level, 1.0f / remaining); // linear in dB, to target
        } else {
            // Falling to silence: the calibration is a 40 dB drop per fall time.
            decay = powf(10.0f, -40.0f / (20.0f * remaining));
        }
        segmentValid = true;
    }

    if (rising) {
        level += slope;
        if (level > target) level = target;
    } else {
        level *= decay;
        if (level < target) level = target;
    }
    // The segment ends on its duration, not on reaching the target. A stage
    // whose target equals the current level is a hold and must still take its
    // time.
    if ((remaining -= 1.0f) <= 0.0f) {
        if (rising || target > 1e-6f) level = target;
        stage++;
        segmentValid = false;
    }
    if (stage == 3 && level <= 1e-6f) stage = 5;   // decayed away before note-off
    if (stage >= 5) level = 0.0f;
    return level;
}

// ----------------------------------------------------------------------- LFO

// p points at the four consecutive LFO bytes: flags, rate, delay, fade.
void Engine::Lfo::begin(const uint8_t* p, uint32_t sr, int ctlDiv, uint32_t seed,
                        float freePhase) {
    const float ctlHz = (float)sr / (float)ctlDiv;
    wave = jv_lfo_wave(p[0]);
    inc = JV_LFO_RATE_HZ((float)p[1]) / ctlHz;
    delayTicks = JV_LFO_DELAY_S((float)p[2]) * ctlHz;
    float fadeT = JV_LFO_FADE_S((float)p[3]) * ctlHz;
    fadeInc = (fadeT > 1.0f) ? 1.0f / fadeT : 1.0f;
    // Key sync restarts at phase 0, which is the unmodulated end; otherwise the
    // voice picks up the free-running phase.
    phase = (p[0] & JV_LFO_KEYSYNC_BIT) ? 0.0f : freePhase;
    ramp = 0.0f;
    rng = seed | 1u;
    held = 1.0f;
    out = 1.0f;
}

void Engine::Lfo::tick() {
    if (delayTicks > 0.0f) { delayTicks -= 1.0f; out = 1.0f; return; }
    if (ramp < 1.0f) { ramp += fadeInc; if (ramp > 1.0f) ramp = 1.0f; }

    float prev = phase;
    phase += inc;
    if (phase >= 1.0f) {
        phase -= (float)(int)phase;
        rng = rng * 1664525u + 1013904223u;          // new value once per period
        held = (float)((rng >> 8) & 0xFFFF) * (1.0f / 65535.0f);
    } else if (prev == 0.0f && held == 1.0f && wave == JV_LFO_WAVE_RND) {
        rng = rng * 1664525u + 1013904223u;
        held = (float)((rng >> 8) & 0xFFFF) * (1.0f / 65535.0f);
    }

    float u;   // 1 = unmodulated, 0 = full modulation
    switch (wave) {
        case JV_LFO_WAVE_SIN: u = 0.5f + 0.5f * cosf(6.28318531f * phase); break;
        case JV_LFO_WAVE_SAW: u = phase; break;
        case JV_LFO_WAVE_SQR: u = (phase < 0.5f) ? 1.0f : 0.0f; break;
        case JV_LFO_WAVE_RND: u = held; break;
        default:              u = fabsf(2.0f * phase - 1.0f); break;   // triangle
    }
    out = 1.0f - (1.0f - u) * ramp;
}

// -------------------------------------------------------------------- filter

float Engine::Filter::run(float in, float g, float k, int mode) {
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = in - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return (mode == JV_TVF_MODE_HPF) ? (in - k * v1 - v2) : v2;
}

// -------------------------------------------------------------------- engine

bool Engine::init(const RomView& rom, uint32_t sampleRate) {
    if (!rom.wave || !rom.rom2 || rom.waveLen < 0x400000 || rom.rom2Len < 0x40000)
        return false;
    rom_ = rom;
    sr_ = sampleRate;
    memset(voices_, 0, sizeof(voices_));
    ageCounter_ = 0;
    return true;
}

bool Engine::selectPatch(int bank, int index) {
    static const uint32_t banks[3] = {JV_BANK_USER, JV_BANK_A, JV_BANK_B};
    if (bank < 0 || bank > 2 || index < 0 || index >= 64) return false;
    patch_ = rom_.rom2 + banks[bank] + (size_t)index * JV_PATCH_SIZE;
    return true;
}

void Engine::setPatch(const uint8_t* patch362) {
    memcpy(patchCopy_, patch362, JV_PATCH_SIZE);
    patch_ = patchCopy_;
}

bool Engine::sampleFor(int waveNumber, uint8_t note, Sample& out) const {
    if (waveNumber < 0 || waveNumber >= JV_MULTI_COUNT) return false;
    const uint8_t* ms = rom_.rom2 + JV_MULTI_TABLE + (size_t)waveNumber * JV_MULTI_STRIDE;
    const uint8_t* splits = ms + 12;
    const uint8_t* idx = ms + 28;

    int zone = 0;
    while (zone < 15 && splits[zone] != 0x7F && note > splits[zone]) zone++;
    uint16_t si = be16(idx + zone * 2);
    if (si == 0xFFFF || si >= JV_SAMPLE_COUNT) return false;

    const uint8_t* s = rom_.rom2 + JV_SAMPLE_TABLE + (size_t)si * JV_SAMPLE_STRIDE;
    out.start = be24(s);
    out.loop = be24(s + 3);
    out.end = be24(s + 6);
    out.rootKey = s[12];
    out.tune = be16(s + 13);
    out.level = s[17];
    return out.start < out.loop && out.loop <= out.end && out.end <= 0x400000;
}

int32_t Engine::decodeStep(Voice& v) const {
    const uint8_t* w = rom_.wave;
    uint32_t a = v.addr;
    int8_t d = (int8_t)w[a];
    // The exponent nibble lives in the first 32 KB of the same 1 MB page,
    // one nibble per 16 samples.
    uint32_t page = a & 0xF00000;
    uint8_t nb = w[page | ((a & 0xFFFFF) >> 5)];
    int nib = (a & 0x10) ? ((nb >> 4) & 15) : (nb & 15);
    int32_t shifted = ((int32_t)d << 11) >> ((10 - nib) & 15);

    v.ref += shifted >> 1;
    if (v.ref > 0x7FFFF) v.ref = 0x7FFFF;
    if (v.ref < -0x80000) v.ref = -0x80000;

    // A pure DPCM integrator does not return to the same value after a loop
    // pass: sample 504's loop drifts by -728 per turn, which walks a sustained
    // tone into the clamp. The chip integrates without correction; a native
    // engine does not have to. Snapshot the accumulator the first time the loop
    // point is passed and restore it on every wrap, which makes the looped
    // waveform exactly periodic.
    ++v.addr;
    if (v.addr == v.smp.loop && !v.loopSeen) {
        v.refAtLoop = v.ref;
        v.loopSeen = true;
    }
    // `end` is INCLUSIVE: the loop spans end-loop+1 samples. Treating it as
    // exclusive detunes every sample by 1731/looplen cents, which is 9 cents on
    // a 193-sample loop and 54 on a 32-sample one -- it was what made the tune
    // field look inconsistent across zones.
    if (v.addr > v.smp.end) {
        v.addr = v.smp.loop;
        if (v.loopSeen) v.ref = v.refAtLoop;
    }
    return v.ref;
}

void Engine::setVoiceLimit(int n) {
    if (n < 1) n = 1;
    if (n > kMaxVoices) n = kMaxVoices;
    voiceLimit_ = n;
}

int Engine::allocVoice() {
    for (int i = 0; i < voiceLimit_; i++)
        if (!voices_[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < voiceLimit_; i++)
        if (voices_[i].age < voices_[oldest].age) oldest = i;
    return oldest;
}

void Engine::updateFilterCoeffs(Voice& v) {
    // Cutoff moves with the TVF envelope, scaled by the bipolar depth. The depth
    // scaling is NOT calibrated -- see tools/jv_extract/README.md.
    float cv = v.cutoffBase +
               v.envDepth * v.tvf.level * JV_TVF_ENV_DEPTH_PER_UNIT + v.cutoffMod;
    float hz = cutoffToHz(cv);
    const float nyq = (float)sr_ * 0.49f;
    if (hz > nyq) hz = nyq;
    if (hz < 20.0f) hz = 20.0f;
    v.coefF = tanf(3.14159265f * hz / (float)sr_);          // g
    float res = v.resonance + v.resMod;
    if (res < 0.0f) res = 0.0f; else if (res > 127.0f) res = 127.0f;
    v.coefQ = JV_TVF_DAMPING(res);                          // k = 1/Q
    if (v.coefQ < 0.08f) v.coefQ = 0.08f;
}

void Engine::updateModulation(Voice& v) {
    // --- modulation matrix -------------------------------------------------
    // Twelve slots, each a signed sensitivity scaled by its source's travel.
    // Destinations 7-10 feed the LFO depths, so they are resolved before the
    // LFOs are ticked; the rest are static offsets.
    const float srcs[3] = { srcMod_ / 127.0f, srcAft_ / 127.0f, srcExp_ / 127.0f };
    float matPitch = 0.0f, matCutoff = 0.0f, matLevelDb = 0.0f, matReso = 0.0f;
    float matPd[2] = {0.0f, 0.0f}, matTvf[2] = {0.0f, 0.0f}, matTva[2] = {0.0f, 0.0f};
    for (int i = 0; i < 12; i++) {
        const float amt = (float)v.matSens[i] * srcs[i / 4];
        if (amt == 0.0f) continue;
        switch (v.matDest[i]) {
            case JV_MOD_DEST_PITCH:  matPitch  += amt * JV_MOD_PITCH_CENTS_PER_UNIT; break;
            case JV_MOD_DEST_CUTOFF: matCutoff += amt * JV_MOD_CUTOFF_CENTS_PER_UNIT; break;
            case JV_MOD_DEST_LEVEL:
                matLevelDb += (amt < 0.0f ? -1.0f : 1.0f) * modLevelDb(amt);
                break;
            case JV_MOD_DEST_RESO: matReso += amt * JV_MOD_RESO_PER_UNIT; break;
            case 5:  matPd[0]  += amt; break;
            case 6:  matPd[1]  += amt; break;
            case 7:  matTvf[0] += amt; break;
            case 8:  matTvf[1] += amt; break;
            case 9:  matTva[0] += amt; break;
            case 10: matTva[1] += amt; break;
            default: break;   // 11-15 produced nothing measurable; see README
        }
    }

    v.lfo[0].tick();
    v.lfo[1].tick();

    float cents = matPitch, tvaDb = matLevelDb, cutoff = 0.0f;
    // The matrix moves the cutoff in cents; the filter wants parameter units.
    const float cutoffParamsFromMatrix = matCutoff / 67.0f;
    for (int i = 0; i < 2; i++) {
        const float m = 1.0f - v.lfo[i].out;      // 0 = none, 1 = full
        // Matrix contributions to the LFO depths are in depth-parameter units.
        const float pd = v.lfoPitchDepth[i] +
                         fmaxf(0.0f, matPd[i]) * JV_MOD_LFO_PITCH_CENTS_PER_UNIT;
        const float td = v.lfoTvaDepth[i] + lfoDepth(JV_LFO_TVA_DEPTH_DB,
                             (uint8_t)fminf(63.0f, fmaxf(0.0f, matTva[i])));
        const float fd = v.lfoTvfDepth[i] + fmaxf(0.0f, matTvf[i]);
        cents  -= pd * m;
        tvaDb  -= td * m;
        cutoff -= fd * m;
    }
    cutoff += cutoffParamsFromMatrix;
    v.resMod = matReso;
    v.inc = (uint32_t)((float)v.baseInc * powf(2.0f, cents * (1.0f / 1200.0f)));
    // Both signs matter: LFO modulation is negative, but a matrix level
    // destination is positive. An earlier version only applied the negative
    // branch, which silently dropped the whole level destination.
    // A negative TVA depth asks for modulation ABOVE the set level, which the
    // machine has no headroom for at full level -- measured as no modulation at
    // all there. Clamping the boost away reproduces that and degrades sensibly
    // at lower levels, where it has not been measured.
    if (tvaDb > 0.0f) tvaDb = 0.0f;
    v.lfoGain = (tvaDb < -0.001f) ? powf(10.0f, tvaDb * (1.0f / 20.0f)) : 1.0f;
    v.cutoffMod = cutoff;
}

void Engine::startVoice(Voice& v, int toneIndex, uint8_t note, uint8_t vel) {
    const uint8_t* t = patch_ + JV_TONE_OFFSET + toneIndex * JV_TONE_SIZE;
    Sample s;
    if (!sampleFor(t[1], note, s)) { v.active = false; return; }

    v.active = true;
    v.note = note;
    v.velocity = vel;
    v.tone = (uint8_t)toneIndex;
    v.smp = s;
    v.addr = s.start;
    v.phase = 0;
    v.ref = 0;
    v.refAtLoop = 0;
    v.loopSeen = false;
    v.s0 = v.s1 = 0;
    v.age = ++ageCounter_;
    v.ctlPhase = 0;

    // Pitch. The sample table's `tune` word is a fine-tune in units of 0.1
    // cent, neutral at 1024 -- established by patching the bytes in rom2 and
    // measuring the reference emulator (0.098-0.100 cents per unit over the low
    // byte, 0.1005 over the high byte). The root byte is exact semitones
    // (-200.0 cents per +2, measured the same way).
    float semis = (float)note - (float)s.rootKey;
    semis += (float)(int8_t)t[37];              // pitchCoarse
    semis += (float)(int8_t)t[38] * 0.01f;      // pitchFine, cents
    float cents = ((float)s.tune - 1024.0f) * 0.1f + kMasterTuneCents;
    float ratio = powf(2.0f, semis * (1.0f / 12.0f) + cents * (1.0f / 1200.0f));
    ratio *= 32000.0f / (float)sr_ * pitchTrim_;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 8.0f) ratio = 8.0f;
    v.baseInc = (uint32_t)(ratio * 65536.0f);
    v.inc = v.baseInc;

    // TVA: patch level, tone level, velocity sensitivity and pan. The sample
    // record's byte +17 is deliberately NOT in here -- patching it across its
    // range moves the machine's output by 0.00 dB, so it is not a level, and
    // multiplying by it wrongly attenuated 233 of the 577 samples.
    float lvl = patchLevelToLinear(patch_[21]) * levelToLinear(t[67]);
    {
        const int8_t vs = (int8_t)t[72];
        const int vmag = vs < 0 ? -vs : vs;
        if (vmag <= JV_LFO_DEPTH_MAX && vmag != 0) {
            float db = (float)vs * (((float)vel - 64.0f) / 64.0f) *
                       JV_TVA_VELO_DB_PER_UNIT *
                       sqrtf((float)t[67] / JV_TVA_VELO_LEVEL_REF);
            if (db < -60.0f) db = -60.0f;
            lvl *= powf(10.0f, db * (1.0f / 20.0f));
        }
    }

    // Patch pan at +22 offsets the tone's own, both centred at 64.
    int pan = t[68] + ((int)patch_[22] - 64);
    if (pan < 0) pan = 0; else if (pan > 127) pan = 127;
    float atten = powf(10.0f, lookup(JV_TVA_PAN_ATTEN_DB, 32, pan) * (1.0f / 20.0f));
    if (pan <= 64) { v.gainL = lvl;         v.gainR = lvl * atten; }
    else           { v.gainL = lvl * atten; v.gainR = lvl; }

    // TVA envelope: times at +74/+76/+78/+80, levels at +75/+77/+79.
    v.tvaT[0] = t[74]; v.tvaT[1] = t[76]; v.tvaT[2] = t[78]; v.tvaT[3] = t[80];
    v.tvaL[0] = t[75]; v.tvaL[1] = t[77]; v.tvaL[2] = t[79];
    v.tva.begin(v.tvaT, v.tvaL, sr_, true);

    // TVF: only engaged when the mode bits select a filter.
    v.filtMode = t[55] & 0x18;
    v.cutoffBase = (float)t[52];
    // Signed like the LFO depths: magnitude 0..63, 64..127 inert, and negative
    // values close the filter instead of opening it.
    {
        const int8_t ed = (int8_t)t[58];
        const int emag = ed < 0 ? -ed : ed;
        v.envDepth = (emag > JV_LFO_DEPTH_MAX) ? 0.0f : (float)ed;
    }
    v.resonance = (float)(t[53] & 0x7F);
    v.filt.reset();
    v.tvfT[0] = t[59]; v.tvfT[1] = t[61]; v.tvfT[2] = t[63]; v.tvfT[3] = t[65];
    v.tvfL[0] = t[60]; v.tvfL[1] = t[62]; v.tvfL[2] = t[64];
    v.tvf.begin(v.tvfT, v.tvfL, sr_, false);

    // LFO1 is bytes +23..+26 with depths at +31/+32/+33; LFO2 is +27..+30 with
    // +34/+35/+36. Seeding per voice keeps the sample-and-hold waveforms from
    // locking together across a chord.
    v.lfo[0].begin(t + 23, sr_, kControlDiv, (uint32_t)(v.age * 2654435761u), freePhase_[0]);
    v.lfo[1].begin(t + 27, sr_, kControlDiv, (uint32_t)(v.age * 40503u + 12345u), freePhase_[1]);
    for (int i = 0; i < 2; i++) {
        freeInc_[i] = v.lfo[i].inc;
        // Bits 3-4 shrink the swing from the bottom; bit 5 is unresolved and
        // deliberately ignored (see jv_calibration.h).
        const float off = JV_LFO_OFFSET_SCALE[(t[23 + i * 4] >> 3) & 3];
        v.lfoPitchDepth[i] = lfoDepth(JV_LFO_PITCH_DEPTH_CENTS, t[31 + i * 3]) * off;
        v.lfoTvaDepth[i]   = lfoDepth(JV_LFO_TVA_DEPTH_DB,      t[33 + i * 3]) * off;
        // TVF depth is uncalibrated; scaled by analogy with the others so that a
        // full depth moves the cutoff by roughly the range the envelope covers.
        const int8_t td = (int8_t)t[32 + i * 3];
        const int tmag = td < 0 ? -td : td;
        v.lfoTvfDepth[i] = (tmag > JV_LFO_DEPTH_MAX) ? 0.0f
                                                     : (float)td * off;
    }
    // Matrix: three source blocks of six bytes at +05, +11, +17. Each is
    // DestAB, DestCD, then SensA..D. Destination A is the LOW nibble of DestAB.
    // Sensitivity is signed and only 0..63 / -63..-1 are live; 64..127 disables.
    for (int src = 0; src < 3; src++) {
        const uint8_t* b = t + 5 + src * 6;
        for (int slot = 0; slot < 4; slot++) {
            const uint8_t dab = b[slot / 2];
            v.matDest[src * 4 + slot] = (slot & 1) ? (uint8_t)(dab >> 4) : (uint8_t)(dab & 15);
            const int8_t sn = (int8_t)b[2 + slot];
            v.matSens[src * 4 + slot] = (sn > JV_MOD_SENS_LIMIT) ? 0 : sn;
        }
    }
    v.lfoGain = 1.0f;
    v.cutoffMod = 0.0f;
    v.resMod = 0.0f;
    updateModulation(v);
    updateFilterCoeffs(v);
}

void Engine::noteOn(uint8_t note, uint8_t velocity) {
    if (!patch_) return;
    for (int tone = 0; tone < 4; tone++) {
        const uint8_t* t = patch_ + JV_TONE_OFFSET + tone * JV_TONE_SIZE;
        if (!(t[0] & 0x80)) continue;                        // tone switched off
        if (velocity < t[3] || velocity > t[4]) continue;    // velocity window
        startVoice(voices_[allocVoice()], tone, note, velocity);
    }
}

void Engine::noteOff(uint8_t note) {
    for (auto& v : voices_)
        if (v.active && v.note == note) { v.tva.release(); v.tvf.release(); }
}

void Engine::allNotesOff() {
    for (auto& v : voices_) v.active = false;
}

int Engine::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) if (v.active) n++;
    return n;
}

void Engine::render(float* left, float* right, int frames) {
    memset(left, 0, sizeof(float) * (size_t)frames);
    memset(right, 0, sizeof(float) * (size_t)frames);

    // The free-running phases run whether or not a voice is using them.
    for (int i = 0; i < 2; i++) {
        freePhase_[i] += freeInc_[i] * ((float)frames / (float)kControlDiv);
        freePhase_[i] -= (float)(int)freePhase_[i];
    }

    for (auto& v : voices_) {
        if (!v.active) continue;
        const bool filtered = (v.filtMode == JV_TVF_MODE_LPF || v.filtMode == JV_TVF_MODE_HPF);

        for (int i = 0; i < frames; i++) {
            v.phase += v.inc;
            int steps = (int)(v.phase >> 16);
            v.phase &= 0xFFFFu;
            while (steps-- > 0) { v.s0 = v.s1; v.s1 = decodeStep(v); }

            float frac = (float)v.phase * (1.0f / 65536.0f);
            // 2^19 is the accumulator's full scale, but the ROM samples only
            // reach 13 % of it (median 7.7 %), so normalising to that throws
            // away 12 dB. This factor is what lines the engine up with the
            // reference: across 24 patches it leaves a mean error near zero.
            float s = ((float)v.s0 + ((float)v.s1 - (float)v.s0) * frac) * (1.0f / 330000.0f);

            if (--v.ctlPhase <= 0) {
                updateModulation(v);
                if (filtered) updateFilterCoeffs(v);
                v.ctlPhase = kControlDiv;
            }
            if (filtered) {
                v.tvf.tick();
                s = v.filt.run(s, v.coefF, v.coefQ, v.filtMode);
            }

            float a = v.tva.tick() * v.lfoGain;
            left[i] += s * a * v.gainL;
            right[i] += s * a * v.gainR;
        }
        if (v.tva.idle()) v.active = false;
    }
}

} // namespace jv
