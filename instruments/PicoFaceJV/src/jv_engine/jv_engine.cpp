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

// There is deliberately no "subtract 64" helper here. The manual's SysEx view
// centres the signed fields at 64, which is tempting to copy, but the ROM bytes
// are plain two's complement -- pitch coarse, the LFO depths, the matrix
// sensitivities and the velocity senses all read correctly as int8_t and all
// have their neutral value at 0. The offset only exists in the MIDI encoding.

// Velocity attenuation: zero at velocity 127, growing as the note gets softer,
// scaled by the tone's sensitivity. Driven by the product of the two.
float velocityAttenDb(int sens, int vel) {
    if (sens <= 0) return 0.0f;
    const float prod = (float)sens * (float)(127 - vel);
    if (prod <= 0.0f) return 0.0f;
    for (int i = 0; i < 9; i++) {
        if (prod <= JV_TVA_VELO_PRODUCT[i + 1]) {
            const float t = (prod - JV_TVA_VELO_PRODUCT[i]) /
                            (JV_TVA_VELO_PRODUCT[i + 1] - JV_TVA_VELO_PRODUCT[i]);
            return JV_TVA_VELO_ATTEN_DB[i] +
                   (JV_TVA_VELO_ATTEN_DB[i + 1] - JV_TVA_VELO_ATTEN_DB[i]) * t;
        }
    }
    return JV_TVA_VELO_ATTEN_DB[9];
}

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

// The machine is at equal temperament. Measured on the reference playing the
// ROM's own sine (multisample 72) at C3, C4 and C5: +0.11, +0.18 and +0.08
// cents. There is no global detune.
//
// This used to carry -9.4 cents, described as a measured property of the
// machine. It was not: 9.4 cents is the residual left over when the sample
// tune words are referenced to a neutral of 1024, which is a statement about
// the tune-word model, and applying it again here counted it twice and left
// the whole instrument 9.65 cents flat. Removing it lands within 0.3 cents of
// the reference. The same shape as the velocity traps below -- a constant that
// belongs to one part of the chain quietly standing in for an error somewhere
// else, invisible until something outside the chain is measured against.
constexpr float kMasterTuneCents = 0.0f;

} // namespace

// ------------------------------------------------------------------ envelope

void Engine::Env::begin(const uint8_t* t, const uint8_t* l, uint32_t sampleRate,
                        bool logLev, float releaseTo) {
    times = t;
    levels = l;
    sr = sampleRate;
    logLevels = logLev;
    releaseLevel = releaseTo;
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
        target = (stage == 4) ? releaseLevel
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
    // Running out of level ends the envelope -- but only for the TVA, where the
    // level IS the gate. The TVF envelope controls a cutoff: a sustain of zero
    // is a legitimate setting there, and forcing the stage past it would slam
    // the filter shut while the note is still sounding. Same for the release
    // target, which the TVF may deliberately land above zero.
    if (logLevels) {
        if (stage == 3 && level <= 1e-6f) stage = 5;   // decayed away before note-off
        if (stage >= 5) level = 0.0f;
    } else if (stage >= 5) {
        level = releaseLevel;
        stage = 5;
    }
    return level;
}

// ------------------------------------------------------------ pitch envelope

// Four segments to four signed levels, then a release to L4. Unlike the TVA and
// TVF envelopes this one ramps linearly in both directions -- the dB decay the
// others use is a property of amplitude, not of pitch -- so the rise timing law
// applies throughout. `used` short-circuits the whole thing for the 471 of 539
// factory tones whose depth is zero.
void Engine::PEnv::begin(const uint8_t* t, const int8_t* l, float controlRate) {
    times = t;
    levels = l;
    ctlRate = controlRate;
    level = 0.0f;
    target = 0.0f;
    slope = 0.0f;
    remaining = 0.0f;
    stage = 0;
    segmentValid = false;
    used = true;
}

void Engine::PEnv::release() {
    if (stage >= 4) return;
    stage = 4;
    segmentValid = false;
}

float Engine::PEnv::tick() {
    if (stage >= 5) return level;
    if (stage == 3) return level;   // hold at L3 until note-off

    if (!segmentValid) {
        const int tv = times[stage < 3 ? stage : 3];
        target = (float)levels[stage < 3 ? stage : 3] * (1.0f / 63.0f);
        if (target < -1.0f) target = -1.0f; else if (target > 1.0f) target = 1.0f;
        remaining = envRiseSeconds(tv) * ctlRate;
        if (remaining < 1.0f) remaining = 1.0f;
        slope = (target - level) / remaining;
        segmentValid = true;
    }

    level += slope;
    if ((remaining -= 1.0f) <= 0.0f) {
        level = target;
        stage++;
        segmentValid = false;
    }
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
    // Fade OUT starts at full depth and ramps away; fade IN is the other way
    // round. Two factory tones use OUT.
    fadeOut = (p[0] & JV_LFO_FADEOUT_BIT) != 0;
    // Key sync restarts at phase 0, which is the unmodulated end; otherwise the
    // voice picks up the free-running phase.
    phase = (p[0] & JV_LFO_KEYSYNC_BIT) ? 0.0f : freePhase;
    ramp = fadeOut ? 1.0f : 0.0f;
    rng = seed | 1u;
    held = 1.0f;
    out = 1.0f;
}

void Engine::Lfo::tick() {
    if (delayTicks > 0.0f) { delayTicks -= 1.0f; out = 1.0f; return; }
    if (fadeOut) { if (ramp > 0.0f) { ramp -= fadeInc; if (ramp < 0.0f) ramp = 0.0f; } }
    else if (ramp < 1.0f)  { ramp += fadeInc; if (ramp > 1.0f) ramp = 1.0f; }

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

void Engine::updateModulation(Voice& v, uint32_t clock) {
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

    // The pitch envelope runs at control rate and adds straight onto the cents.
    // Skipped entirely when the depth is zero, which is 471 of 539 tones.
    float cents = matPitch, tvaDb = matLevelDb, cutoff = 0.0f;
    if (v.penv.used) cents += v.penv.tick() * v.penvDepthSemis * 100.0f;
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
    float rate = powf(2.0f, cents * (1.0f / 1200.0f));
    // FXM: a fixed 125 Hz square on the playback rate. Measured as frequency
    // modulation -- the fractional deviation is what stays constant across the
    // keyboard, not the modulation index -- and the modulator is the sample
    // clock divided down, so it is phased off the global counter rather than
    // restarted per note. Half a period is sr/250 samples.
    if (v.fxmK > 0.0f) {
        const uint32_t half = sr_ / 250u;
        rate *= ((clock / half) & 1u) ? (1.0f - v.fxmK) : (1.0f + v.fxmK);
    }
    v.inc = (uint32_t)((float)v.baseInc * rate);
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
    // Pitch keyfollow scales how far the keyboard moves the pitch, about C4:
    // +100 % is the normal semitone per key, 0 % pins every key to the pitch C4
    // would sound. The multisample zone is still picked by the key actually
    // played -- only the playback rate is scaled. 507 of the 539 factory tones
    // sit at +100 %, so this changes nothing for most of them.
    const float pkf = jv_kf16(t[40] & 15) * 0.01f;
    const float effNote = 60.0f + ((float)note - 60.0f) * pkf;
    float semis = effNote - (float)s.rootKey;
    semis += (float)(int8_t)t[37];              // pitchCoarse
    semis += (float)(int8_t)t[38] * 0.01f;      // pitchFine, cents
    float cents = ((float)s.tune - 1024.0f) * 0.1f + kMasterTuneCents;

    // Random pitch, redrawn per note, and the patch-common analog feel. Both
    // exist to stop tones of the same patch phase-locking into a fixed beat;
    // without them a multi-tone pad sits at exactly the same interval every
    // note and throbs. The random-pitch amounts are the manual's table and the
    // spread is taken as symmetric about the nominal pitch. Analog feel has no
    // documented magnitude -- the values in jv_calibration.h are a guess -- but
    // Roland's own notes say it varies "pitch AND LEVEL", so it does both here.
    float analogLevel = 1.0f;
    {
        uint32_t r = (v.age * 2654435761u) ^ (note * 40503u) ^ 0x9E3779B9u;
        r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
        const float u = (float)((r >> 8) & 0xFFFF) * (1.0f / 32767.5f) - 1.0f;  // -1..+1
        cents += u * JV_RANDOM_PITCH_CENTS[t[39] & 15] * 0.5f;
        const float feel = (float)patch_[20] * (1.0f / 127.0f);
        r ^= r >> 16; r *= 3266489917u; r ^= r >> 16;
        const float u2 = (float)((r >> 8) & 0xFFFF) * (1.0f / 32767.5f) - 1.0f;
        cents += u2 * feel * JV_ANALOG_FEEL_CENTS;
        r ^= r >> 15; r *= 2654435761u; r ^= r >> 13;
        const float u3 = (float)((r >> 8) & 0xFFFF) * (1.0f / 32767.5f) - 1.0f;
        analogLevel = powf(10.0f, u3 * feel * JV_ANALOG_FEEL_DB * (1.0f / 20.0f));
    }

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
    float lvl = patchLevelToLinear(patch_[21]) * levelToLinear(t[67]) * analogLevel;

    // The output dry level scales the direct path on its own, separately from
    // the sends. 31 of 539 factory tones set it below 127 and six of those sit
    // at or near zero: on the hardware those tones are heard only through the
    // chorus or reverb. With no effects yet they simply drop out, which is what
    // the dry path does -- no patch loses all four tones this way.
    lvl *= levelToLinear(t[81]);

    // Velocity: the curve warps velocity first, then the sensitivity law acts
    // on the warped value. Curve 1 (stored 0) is the straight line the whole
    // velocity calibration was measured on, so it stays an exact identity.
    const int velA = (int)(jv_velocity_curve(t[71] & 7, vel) + 0.5f);
    {
        // Signed, magnitude up to 63; the ROM byte is two's complement where
        // the SysEx view adds 64. Positive makes soft notes quieter, negative
        // does the reverse -- only one factory tone uses the negative side, but
        // it costs nothing to get right.
        const int8_t vs = (int8_t)t[72];
        if (vs > 0 && vs <= JV_MOD_SENS_LIMIT)
            lvl *= powf(10.0f, -velocityAttenDb(vs, velA) * (1.0f / 20.0f));
        else if (vs < 0 && vs >= -JV_MOD_SENS_LIMIT)
            lvl *= powf(10.0f, -velocityAttenDb(-vs, 127 - velA) * (1.0f / 20.0f));
    }

    // Level keyfollow, the low nibble of +70. Which nibble holds which is not
    // measured; the high one is taken as the TVA envelope's time keyfollow
    // because +40 and +54 both pack a time keyfollow high and the main
    // keyfollow low. Both nibbles are neutral at 7 on the great majority of
    // tones, so a wrong guess would show up on few of them either way.
    {
        const float lkf = jv_kf15(t[70] & 15) * 0.01f;
        if (lkf != 0.0f) {
            const float db = lkf * ((float)note - 60.0f) * JV_LEVEL_KF_DB_PER_SEMITONE;
            lvl *= powf(10.0f, db * (1.0f / 20.0f));
        }
    }

    // Pan: 128 alternates left/right from note to note, above that is centre,
    // and only 0..127 is the continuous law. Clamping everything to 127 sent a
    // quarter of the factory tones hard right.
    int pan;
    if (t[68] == JV_PAN_ALTERNATING)      pan = (panAlt_ & 1) ? JV_PAN_ALT_B : JV_PAN_ALT_A;
    else if (t[68] > JV_PAN_ALTERNATING)  pan = 64;
    else                                  pan = t[68];
    // Panning keyfollow walks the image across the keyboard about C4 -- the
    // trick that gives a piano patch its stereo spread. High nibble of +39,
    // measured; the per-semitone amount is not.
    pan += (int)(jv_kf15(t[39] >> 4) * 0.01f * ((float)note - 60.0f) *
                 JV_PAN_KF_UNITS_PER_SEMITONE);
    pan += (int)patch_[22] - 64;
    if (pan < 0) pan = 0; else if (pan > 127) pan = 127;
    float atten = powf(10.0f, lookup(JV_TVA_PAN_ATTEN_DB, 32, pan) * (1.0f / 20.0f));
    if (pan <= 64) { v.gainL = lvl;         v.gainR = lvl * atten; }
    else           { v.gainL = lvl * atten; v.gainR = lvl; }

    // TVA envelope: times at +74/+76/+78/+80, levels at +75/+77/+79.
    v.tvaT[0] = t[74]; v.tvaT[1] = t[76]; v.tvaT[2] = t[78]; v.tvaT[3] = t[80];
    v.tvaL[0] = t[75]; v.tvaL[1] = t[77]; v.tvaL[2] = t[79];
    v.tva.begin(v.tvaT, v.tvaL, sr_, true, 0.0f);   // the TVA always ends at silence

    // TVF: only engaged when the mode bits select a filter.
    v.filtMode = t[55] & 0x18;
    // Cutoff keyfollow, low nibble of +54. At +100 % the corner tracks the
    // keyboard one for one about C4; the conversion falls straight out of the
    // measured cutoff law, which is 17.93 parameter units per octave. 185 of
    // 539 tones set this away from zero, and without it every one of them
    // filters at the same absolute frequency whatever key is played.
    v.cutoffBase = (float)t[52] +
                   jv_kf16(t[54] & 15) * 0.01f * ((float)note - 60.0f) *
                       JV_CUTOFF_UNITS_PER_SEMITONE;
    // Signed like the LFO depths: magnitude 0..63, 64..127 inert, and negative
    // values close the filter instead of opening it.
    {
        const int8_t ed = (int8_t)t[58];
        const int emag = ed < 0 ? -ed : ed;
        v.envDepth = (emag > JV_LFO_DEPTH_MAX) ? 0.0f : (float)ed;
    }
    // TVF envelope velocity sensitivity (+56), the most widely used of the
    // parameters that were missing: 336 of 539 tones set it. Positive means a
    // harder note opens the filter further, negative inverts that. Measured --
    // see jv_calibration.h, and note that the sensitivity that takes the depth
    // to zero is 32, not the 63 the other bipolar fields use.
    {
        const int8_t fvs = (int8_t)t[56];
        if (fvs != 0 && fvs >= -JV_MOD_SENS_LIMIT && fvs <= JV_MOD_SENS_LIMIT)
            v.envDepth *= jv_tvf_velocity_scale(fvs, jv_velocity_curve(t[55] & 7, vel));
    }
    v.resonance = (float)(t[53] & 0x7F);
    v.filt.reset();
    v.tvfT[0] = t[59]; v.tvfT[1] = t[61]; v.tvfT[2] = t[63]; v.tvfT[3] = t[65];
    v.tvfL[0] = t[60]; v.tvfL[1] = t[62]; v.tvfL[2] = t[64];
    // ... and the TVF envelope releases to its own fourth level (+66), not to
    // zero. 95 tones set it non-zero, and releasing those to zero shut the
    // filter on note-off while the TVA tail was still sounding.
    v.tvf.begin(v.tvfT, v.tvfL, sr_, false, tvfEnvLevel(t[66]));

    // Pitch envelope. Depth is signed semitones, +-12 at the limit; 68 of 539
    // tones use it, 33 of them at full depth.
    {
        int pd = (int8_t)t[43];
        if (pd < -12) pd = -12; else if (pd > 12) pd = 12;
        v.penvDepthSemis = (float)pd;
        v.penvT[0] = t[44]; v.penvT[1] = t[46]; v.penvT[2] = t[48]; v.penvT[3] = t[50];
        v.penvL[0] = (int8_t)t[45]; v.penvL[1] = (int8_t)t[47];
        v.penvL[2] = (int8_t)t[49]; v.penvL[3] = (int8_t)t[51];
        v.penv.begin(v.penvT, v.penvL, (float)sr_ / (float)kControlDiv);
        v.penv.used = (pd != 0);
    }

    // LFO1 is bytes +23..+26 with depths at +31/+32/+33; LFO2 is +27..+30 with
    // +34/+35/+36. Seeding per voice keeps the sample-and-hold waveforms from
    // locking together across a chord.
    v.lfo[0].begin(t + 23, sr_, kControlDiv, (uint32_t)(v.age * 2654435761u), freePhase_[0]);
    v.lfo[1].begin(t + 27, sr_, kControlDiv, (uint32_t)(v.age * 40503u + 12345u), freePhase_[1]);
    for (int i = 0; i < 2; i++) {
        freeInc_[i] = v.lfo[i].inc;
        // Bits 3-5 are the offset, neutral at 2 rather than 0 -- see
        // jv_calibration.h for why the depth tables and this scale cancel
        // there, and why index 4 silences the modulation.
        const float off = JV_LFO_OFFSET_SCALE[(t[23 + i * 4] >> 3) & 7];
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
    // FXM (+02): bit 7 switches it on, bits 0-3 are the depth. The panel shows
    // the depth one higher than it is stored, and the measured deviation is
    // proportional to that displayed value.
    v.fxmK = (t[2] & JV_FXM_SWITCH_BIT)
                 ? JV_FXM_K_PER_STEP * (float)((t[2] & JV_FXM_DEPTH_MASK) + 1)
                 : 0.0f;

    v.lfoGain = 1.0f;
    v.cutoffMod = 0.0f;
    v.resMod = 0.0f;
    updateModulation(v, clock_);
    updateFilterCoeffs(v);
}

void Engine::noteOn(uint8_t note, uint8_t velocity) {
    if (!patch_) return;
    ++panAlt_;   // one step per note, so alternating tones swap sides
    for (int tone = 0; tone < 4; tone++) {
        const uint8_t* t = patch_ + JV_TONE_OFFSET + tone * JV_TONE_SIZE;
        if (!(t[0] & 0x80)) continue;                        // tone switched off
        if (velocity < t[3] || velocity > t[4]) continue;    // velocity window
        startVoice(voices_[allocVoice()], tone, note, velocity);
    }
}

void Engine::noteOff(uint8_t note) {
    for (auto& v : voices_)
        if (v.active && v.note == note) { v.tva.release(); v.tvf.release(); v.penv.release(); }
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
            // reference. It has now moved twice for the same reason: an error
            // in the velocity law is indistinguishable from a level offset
            // unless the comparison is made at more than one velocity. It first
            // moved 10.5 dB when the law's direction was corrected, and 1.0 dB
            // again when the velocity CURVES were measured -- the residual
            // error is now 0.6 dB at velocity 127 and 1.1 dB at velocity 100,
            // where before the curves it was flat at one and 4-5 dB out at the
            // other. Fitted over all 128 factory patches at both velocities.
            float s = ((float)v.s0 + ((float)v.s1 - (float)v.s0) * frac) * (1.0f / 88234.0f);

            if (--v.ctlPhase <= 0) {
                updateModulation(v, clock_ + (uint32_t)i);
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
    clock_ += (uint32_t)frames;
}

} // namespace jv
