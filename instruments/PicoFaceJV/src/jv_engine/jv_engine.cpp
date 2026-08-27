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

// The filter coefficient the firmware writes, read out of the reference's
// ram2[11] over 16384 for cutoff 0, 4, 8 ... 124. It climbs by exactly 128 per
// cutoff unit up to about 32, then accelerates, and saturates from 88 upward --
// past that the cutoff byte does nothing at all. See tools/jv_extract/README.md.
static const float JV_TVF_F[32] = {
    0.0078f, 0.0391f, 0.0703f, 0.1016f, 0.1328f, 0.1641f, 0.1953f, 0.2266f,
    0.2578f, 0.2891f, 0.3281f, 0.3672f, 0.4062f, 0.4531f, 0.5078f, 0.5625f,
    0.6172f, 0.6797f, 0.7500f, 0.8125f, 0.8906f, 0.9609f, 1.0000f, 1.0000f,
    1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 1.0000f
};
static float tvfCoefF(float cv) {
    if (cv <= 0.0f) return JV_TVF_F[0];
    if (cv >= 124.0f) return 1.0f;
    const float x = cv * 0.25f;
    const int i = (int)x;
    const float t = x - (float)i;
    return JV_TVF_F[i] + (JV_TVF_F[i + 1] - JV_TVF_F[i]) * t;
}

// The chip does not join two samples with a straight line. It carries a
// three-entry weight table indexed by the fractional position and applies it to
// the DPCM deltas, which comes to a four-point kernel over the decoded samples:
// at fractional position zero the weights are 0.174 / 0.653 / 0.173 / 0 rather
// than 0 / 1 / 0 / 0, so it is smoothing even where a linear interpolator would
// pass the sample through untouched. Read off the reference's table and turned
// back into kernel form, those weights are a cubic B-spline to within 0.014 --
// mean 0.008 -- across all 128 steps, so the formula stands in for the table and
// nothing has to be copied out of it.
//
// The difference is audible because it is a filter. At fractional position zero
// the spline is -2.3 dB at 6.4 kHz, -5.0 at 9.6 and -9.5 at 16 kHz where linear
// interpolation is flat; that missing lowpass is why this engine ran bright.
// 128 steps is the chip's own resolution, so the table is indexed the same way.
static float kBSpline[128][4];
static void buildBSpline() {
    for (int i = 0; i < 128; i++) {
        const float t = (float)i / 128.0f, u = 1.0f - t;
        kBSpline[i][0] = u * u * u * (1.0f / 6.0f);
        kBSpline[i][1] = (3.0f * t * t * t - 6.0f * t * t + 4.0f) * (1.0f / 6.0f);
        kBSpline[i][3] = t * t * t * (1.0f / 6.0f);
        // The four weights sum to one, so the last is what the others leave.
        kBSpline[i][2] = 1.0f - kBSpline[i][0] - kBSpline[i][1] - kBSpline[i][3];
    }
}

// How far a segment falling to silence travels over one fall time. The fall
// table itself is measured; this is the depth that rides on it, and it is 32.6
// dB, not the 40 that stood here before.
//
// Measured against the reference on a synthetic patch -- one tone, one looping
// wave, held at full level with every modulator neutral -- by timing a 12 dB
// drop after the body has run out and only the envelope is still working. At 40
// the engine reached that point in 0.81..0.85 of the reference's time across
// stage-2 times 70..110, and its release in 0.74..0.79. Sweeping the figure
// crosses unity between 32 and 34 (34 gives 0.96, 32 gives 1.02); 32.6 lands
// the decay at 0.99..1.03 and the release at 0.95..0.97, the remaining release
// gap being the 0.1 s offset the measurement window starts with.
//
// Both stages ride on this: stage 2 whenever its level is zero, which is how
// most percussive patches are built, and every release to silence. The rise
// side is untouched and stays exact -- at stage-1 time 70 the engine tracks the
// reference within 1 % at every point of the ramp.
#ifndef JV_FALL_TO_SILENCE_DB
#define JV_FALL_TO_SILENCE_DB 32.6f
#endif

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
    for (int i = 0; i < JV_TVA_VELO_POINTS - 1; i++) {
        if (prod <= JV_TVA_VELO_PRODUCT[i + 1]) {
            const float t = (prod - JV_TVA_VELO_PRODUCT[i]) /
                            (JV_TVA_VELO_PRODUCT[i + 1] - JV_TVA_VELO_PRODUCT[i]);
            return JV_TVA_VELO_ATTEN_DB[i] +
                   (JV_TVA_VELO_ATTEN_DB[i + 1] - JV_TVA_VELO_ATTEN_DB[i]) * t;
        }
    }
    return JV_TVA_VELO_ATTEN_DB[JV_TVA_VELO_POINTS - 1];
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
            // Falling to silence: a fixed dB drop per fall time. Overridable at
            // compile time so the figure can be swept without editing here.
            decay = powf(10.0f, -JV_FALL_TO_SILENCE_DB / (20.0f * remaining));
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

// The chip's filter, in the chip's form: a naive state-variable filter with a
// sample of delay in the loop, exactly as the reference's PCM core runs it --
//
//     lp += f * bp;   hp = in - lp - q * bp;   bp += f * hp;
//
// with the mode bit picking the low-pass state or the high-pass term, which is
// what it does with `ram1[3]` against `v3`.
//
// This replaced a zero-delay (TPT) filter, and the reason matters. A zero-delay
// filter holds its Q as the corner moves; the naive one sharpens, and the
// reference sharpens with it. Measured through white noise, the old filter's
// resonant peak matched at cutoff 40 and fell 2.7 to 4.8 dB short at cutoffs 60
// and 80 across resonance 60, 100 and 127 -- the shape of a Q that should have
// been climbing.
//
// An earlier attempt at this swap failed and is worth remembering: it kept the
// old coefficient, f = 2 sin(pi fc/fs), which runs to 1.98. The naive form is
// only stable while f + q stays under 2, so that either had to be capped --
// darkening the bright half of the bank -- or left to self-oscillate, which sent
// five patches to negative correlation. The chip never has that problem because
// its own coefficient SATURATES AT 1.0, read straight out of ram2[11]. With the
// measured table below, f + q reaches 2.0 only at the single corner where
// resonance is 0 and the cutoff is wide open, and never crosses it.
float Engine::Filter::run(float in, float f, float q, int mode) {
    lp += f * bp;
    const float hp = in - lp - q * bp;
    bp += f * hp;
    return (mode == JV_TVF_MODE_HPF) ? hp : lp;
}

// -------------------------------------------------------------------- chorus

void Engine::Chorus::reset() {
    for (int i = 0; i < JV_CHORUS_MAX_DELAY; i++) bufL[i] = bufR[i] = 0.0f;
    pos = 0;
    phase = 0.0f;
}

// p points at the 14 patch-common bytes; the chorus fields are +4 (type, in
// bits 4-5 of the shared reverb/chorus byte), +16 level, +17 depth, +18 rate,
// +19 feedback -- indices here are relative to the patch, not the block.
void Engine::Chorus::configure(const uint8_t* patch, uint32_t sr) {
    const int lvlByte = patch[16];
    level    = (float)(lvlByte & 0x7F) * (JV_CHORUS_LEVEL_GAIN / 127.0f);
    toReverb = (lvlByte & JV_CHORUS_TO_REVERB_BIT) != 0;
    feedback = JV_CHORUS_FEEDBACK(patch[19]);

    float hz = JV_CHORUS_RATE_HZ(patch[18]);
    float slope = JV_CHORUS_SLOPE(patch[17]);   // samples per second
    // CHORUS2 is the same delay six times deeper and twice as fast; 22 of the
    // 192 factory patches use it. See jv_calibration.h.
    if (((patch[12] >> 4) & 3) == JV_CHORUS_TYPE2) {
        slope *= JV_CHORUS2_SLOPE_MUL;
        hz    *= JV_CHORUS2_RATE_MUL;
    }
    inc = hz / (float)sr;
    // The excursion is what the slope and the turn-around rate imply: the delay
    // slides at `slope` and reverses twice per cycle.
    excursion = slope / (2.0f * hz);
    const float room = (float)JV_CHORUS_MAX_DELAY - JV_CHORUS_BASE_DELAY - 2.0f;
    if (excursion > room) excursion = room;
}

void Engine::Chorus::process(const float* inL, const float* inR,
                             float* left, float* right, int n) {
    if (level <= 0.0f) return;
    for (int i = 0; i < n; i++) {
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        // Triangle, 0..1, and the right channel half a cycle behind it. The two
        // channels measured as exact antiphase with a common minimum.
        const float tl = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
        const float tr = 1.0f - tl;

        const float dl = JV_CHORUS_BASE_DELAY + tl * excursion;
        const float dr = JV_CHORUS_BASE_DELAY + tr * excursion;

        // Bias the read position up by a whole buffer so it can never go
        // negative: truncation toward zero on a negative value yields a
        // NEGATIVE fraction, which makes the interpolator extrapolate backwards
        // instead of between the two samples. With the write pointer cycling
        // 0..767 and the delay sitting at 578..761 that happened three quarters
        // of the time, and the output stopped resembling a delayed copy at all
        // -- correlation against the dry signal fell to 0.13.
        const float bias = (float)JV_CHORUS_MAX_DELAY;
        float outL, outR;
        {
            const float fp = (float)pos - dl + bias;
            const int   ip = (int)fp;
            const float fr = fp - (float)ip;
            const int   a = ip % JV_CHORUS_MAX_DELAY;
            const int   b = (a + 1) % JV_CHORUS_MAX_DELAY;
            outL = bufL[a] + (bufL[b] - bufL[a]) * fr;
        }
        {
            const float fp = (float)pos - dr + bias;
            const int   ip = (int)fp;
            const float fr = fp - (float)ip;
            const int   a = ip % JV_CHORUS_MAX_DELAY;
            const int   b = (a + 1) % JV_CHORUS_MAX_DELAY;
            outR = bufR[a] + (bufR[b] - bufR[a]) * fr;
        }

        bufL[pos] = inL[i] + outL * feedback;
        bufR[pos] = inR[i] + outR * feedback;
        if (++pos >= JV_CHORUS_MAX_DELAY) pos = 0;

        left[i]  += outL * level;
        right[i] += outR * level;
    }
}

// -------------------------------------------------------------------- reverb

void Engine::Reverb::reset() {
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < kCombs; k++) {
            for (int i = 0; i < kCombMax; i++) comb[c][k][i] = 0.0f;
            combPos[c][k] = 0;
        }
        for (int k = 0; k < 2; k++) {
            for (int i = 0; i < kApMax; i++) ap[c][k][i] = 0.0f;
            apPos[c][k] = 0;
        }
    }
    for (int i = 0; i < kDelayMax; i++) line[i] = 0.0f;
    linePos = 0;
    panTap = 0;
    panCount = 0;
}

void Engine::Reverb::configure(const uint8_t* patch, uint32_t sr) {
    // The reverb type is bits 0-2, NOT the low nibble. The manual gives the
    // range as 0-7, and bit 3 is something else -- 40 of the 192 factory
    // patches set it. Masking with 0x0F handed 67 of them a type of 6 or 7,
    // which is DELAY or PAN-DLY, so a third of the bank played a hard echo
    // where it should have had a room or a hall. That is exactly what it
    // sounded like.
    type  = patch[12] & 0x07;
    // The delays pass the signal far more directly than the reverb network, so
    // they carry their own full-scale gain.
    const float g = (type >= JV_REVERB_TYPE_DELAY)
                        ? JV_DELAY_LEVEL_GAIN
                        : jv_reverb_level(type, patch[14]);
    level = (float)patch[13] * (g / 127.0f);
    feedback = JV_DELAY_FEEDBACK(patch[15]);
    if (feedback < 0.0f) feedback = 0.0f;

    if (type >= JV_REVERB_TYPE_DELAY) {
        const float ms = JV_DELAY_MS(patch[14]);
        delaySamples = (int)(ms * 0.001f * (float)sr + 0.5f);
        if (delaySamples < 1) delaySamples = 1;
        if (delaySamples > kDelayMax - 1) delaySamples = kDelayMax - 1;
        return;
    }

    // The two channels run quite DIFFERENT comb sets, not the same set nudged
    // by a few samples. The reference's tail measures |L/R correlation| < 0.04;
    // a small stereo spread (809/823, 877/887, ...) left the engine at +0.85,
    // because both sides see the same input and nearly the same delays.
    static const int kCombL[kCombs] = {  809,  877,  937, 1049 };
    static const int kCombR[kCombs] = { 1123, 1187, 1259, 1381 };
    static const int kApL[2] = { 337, 113 };
    static const int kApR[2] = { 241, 173 };
    for (int k = 0; k < kCombs; k++) {
        combLen[0][k] = kCombL[k];
        combLen[1][k] = kCombR[k];
    }
    apLen[0][0] = kApL[0]; apLen[0][1] = kApL[1];
    apLen[1][0] = kApR[0]; apLen[1][1] = kApR[1];

    // Set each comb's feedback so the network decays to -60 dB in the measured
    // time: a comb of length D repeating with gain g reaches -60 dB after
    // RT60/D repeats, so g = 10^(-3 D / (RT60 * sr)).
    // Per channel, since the two comb sets have different lengths and the same
    // gain would give them different decay times.
    // JV_REVERB_TIME_MS is fitted to the burst response between -5 and -45 dB,
    // but the reference's decay is NOT a single exponential: measured in
    // context on A04 it steepens from 7 to 15.6 dB per 100 ms as the tail dies,
    // where a comb bank falls at a constant 8.9. Matching the early slope
    // therefore leaves the late tail hanging 6 to 17 dB high, which is what it
    // sounds like -- a delay that will not stop. Scaling the target to the
    // decay measured in context (561 ms against a nominal 743 on A04) puts the
    // audible part right; the first 100 ms of tail then runs slightly short.
    const float rt = jv_reverb_rt60_ms(type, patch[14]) * 0.001f;
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < kCombs; k++) {
            const float d = (float)combLen[c][k] / (float)sr;
            float g = powf(10.0f, -3.0f * d / (rt > 0.001f ? rt : 0.001f));
            if (g > 0.985f) g = 0.985f;
            combG[c][k] = g;
        }
    }
}

void Engine::Reverb::process(const float* inL, const float* inR,
                             float* left, float* right, int n) {
    if (level <= 0.0f) return;

    if (type >= JV_REVERB_TYPE_DELAY) {
        // PAN-DLY is TWO taps on one line, not one tap that alternates sides:
        // at feedback 0 the reference already produces two echoes, 62 ms left
        // and 124 ms right at time 32. So the half-period tap feeds the left
        // channel, the full-period tap the right, and the feedback goes round
        // the full period -- which is why the levels come in equal pairs
        // (0.166, 0.166, 0.080, 0.080, 0.039, 0.039 at feedback 64).
        const bool pan = (type == JV_REVERB_TYPE_PANDLY);
        const int half = delaySamples / 2;
        for (int i = 0; i < n; i++) {
            int r = linePos - delaySamples;
            if (r < 0) r += kDelayMax;
            const float out = line[r];
            line[linePos] = 0.5f * (inL[i] + inR[i]) + out * feedback;
            if (pan) {
                int rh = linePos - half;
                if (rh < 0) rh += kDelayMax;
                left[i]  += line[rh] * level;
                right[i] += out * level;
            } else {
                left[i]  += out * level;
                right[i] += out * level;
            }
            if (++linePos >= kDelayMax) linePos = 0;
        }
        return;
    }

    for (int i = 0; i < n; i++) {
        const float in[2] = { inL[i], inR[i] };
        for (int c = 0; c < 2; c++) {
            float acc = 0.0f;
            for (int k = 0; k < kCombs; k++) {
                const int len = combLen[c][k];
                int& p = combPos[c][k];
                const float y = comb[c][k][p];
                comb[c][k][p] = in[c] + y * combG[c][k];
                if (++p >= len) p = 0;
                acc += y;
            }
            acc *= 0.25f;
            for (int k = 0; k < 2; k++) {
                const int len = apLen[c][k];
                int& p = apPos[c][k];
                const float y = ap[c][k][p];
                const float v = acc + y * 0.5f;
                ap[c][k][p] = v;
                if (++p >= len) p = 0;
                acc = y - v * 0.5f;
            }
            (c == 0 ? left : right)[i] += acc * level;
        }
    }
}

// -------------------------------------------------------------------- engine

bool Engine::init(const RomView& rom, uint32_t sampleRate) {
    // The wave blob need not be the full 4 MB: a build that carries only the
    // samples some banks reach is shorter, and the sample table it ships with
    // has been rewritten to match. Every address is bounds-checked against
    // waveLen in sampleFor(), so the length here only has to be plausible --
    // one page's worth of exponent nibbles is the floor.
    if (!rom.wave || !rom.rom2 || rom.waveLen < 0x8000 || rom.rom2Len < 0x40000)
        return false;
    buildBSpline();
    rom_ = rom;
    sr_ = sampleRate;
    memset(voices_, 0, sizeof(voices_));
    ageCounter_ = 0;
    return true;
}

bool Engine::selectPatch(int bank, int index) {
    static const uint32_t banks[3] = {JV_BANK_USER, JV_BANK_A, JV_BANK_B};
    if (!rom_.rom2) return false;   // init() refused the ROM; do not walk off it
    if (bank < 0 || bank > 2 || index < 0 || index >= 64) return false;
    patch_ = rom_.rom2 + banks[bank] + (size_t)index * JV_PATCH_SIZE;
    chorus_.configure(patch_, sr_);
    chorus_.reset();
    reverb_.configure(patch_, sr_);
    reverb_.reset();
    return true;
}

void Engine::setPatch(const uint8_t* patch362) {
    memcpy(patchCopy_, patch362, JV_PATCH_SIZE);
    patch_ = patchCopy_;
    chorus_.configure(patch_, sr_);
    chorus_.reset();
    reverb_.configure(patch_, sr_);
    reverb_.reset();
}

// The velocity curve tables, straight out of the machine's own ROM.
int Engine::velocityIndex(int curve, uint8_t vel) const {
    if (!rom_.rom2 || rom_.rom2Len < JV_VELO_CURVE_BASE + 7 * 128)
        return vel;                       // no ROM to read: leave velocity alone
    if (curve < 0 || curve > 6) curve = 0;   // 7 is not a curve; the table ends
    const uint8_t b = rom_.rom2[JV_VELO_CURVE_BASE + curve * 128 + (vel & 0x7F)];
    return 127 - (int)(b >> 1);
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

    // A zone list can end in a terminator rather than a zone. One sample -- a
    // twenty-frame body looping over a single frame, which can sustain
    // nothing -- appears in nine multisamples and is the last occupied zone in
    // every one of them. All nine are the basses. Reading it as a playable
    // zone left eight bank-A patches silent above note 84 while the reference
    // played on, in tune, from the zone below: measured on St Fretless, the
    // reference tracks 276, 311, 369, 440, 522 Hz at notes 85 to 96 where this
    // engine produced nothing at all.
    //
    // It is recognised rather than hardcoded by number: of 577 samples it is
    // the only one that both loops over a single frame and is shorter than a
    // quarter-second's worth of anything -- the other 41 single-frame loops
    // are one-shots of 298 frames and up, and every other short sample has a
    // real loop. Fall back to the zone below, which is what the machine plays.
    for (int guard = 0; guard < 16 && zone > 0; ++guard) {
        const uint8_t* c = rom_.rom2 + JV_SAMPLE_TABLE + (size_t)si * JV_SAMPLE_STRIDE;
        const uint32_t cs = be24(c), cl = be24(c + 3), ce = be24(c + 6);
        if (!(cl == ce && ce - cs < 64)) break;      // a real zone, keep it
        si = be16(idx + --zone * 2);
        if (si == 0xFFFF || si >= JV_SAMPLE_COUNT) return false;
    }

    const uint8_t* s = rom_.rom2 + JV_SAMPLE_TABLE + (size_t)si * JV_SAMPLE_STRIDE;
    out.start = be24(s);
    out.loop = be24(s + 3);
    out.end = be24(s + 6);
    out.rootKey = s[12];
    out.tune = be16(s + 13);
    out.level = s[17];
    // Flag byte +11. Bit 0 marks an alternating loop; bit 1 marks a one-shot
    // (all 42 samples carrying it have loop == end, so the forward path already
    // holds on the last value). An alternating loop needs at least two samples
    // to turn around in.
    out.bidir = (s[11] & 1) != 0 && out.end > out.loop + 1;
    // Reverse plays the body once, backwards, and never reaches a loop -- so it
    // overrides the alternating bit, which the REV records carry anyway
    // (0x05 = both) because they were cut from the forward samples.
    out.reverse = (s[11] & 4) != 0;
    if (out.reverse) out.bidir = false;
    // `end` is inclusive, so it is the last address read. A cut-down build
    // zeroes the entries of the samples it dropped, which fails start < loop
    // here and leaves the tone silent rather than playing whatever now lives
    // at that address.
    return out.start < out.loop && out.loop <= out.end && out.end < rom_.waveLen;
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
    const int32_t delta = (((int32_t)d << 11) >> ((10 - nib) & 15)) >> 1;

    if (v.dir < 0 && v.smp.reverse) {
        // Playing the body backwards. The integrator keeps ADDING while the
        // address walks down, exactly as it does on the return leg of an
        // alternating loop, so what comes out is the forward waveform negated
        // and read back to front -- which is the reverse crash, the sign being
        // inaudible.
        v.ref += delta;
        if (v.ref > 0x7FFFF) v.ref = 0x7FFFF;
        if (v.ref < -0x80000) v.ref = -0x80000;
        // `start` is the forward attack, so the swell ends there and the voice
        // ends with it: the reference falls to digital silence rather than
        // holding the last value or looping.
        if (v.addr <= v.smp.start) { v.active = false; return 0; }
        --v.addr;
        return v.ref;
    }

    if (v.dir < 0) {
        // Retracing an alternating loop. The chip does NOT undo the steps: it
        // keeps integrating in the same direction while the address walks
        // backwards, so what comes out is 2*v(end) - v(mirrored) rather than
        // v(mirrored). Since the loop endpoints sit on zero crossings -- v(end)
        // is exactly 0 for the samples checked -- that is the negated mirror.
        //
        // Subtracting instead, which is the arithmetically "correct" way to
        // retrace a differential stream, gives the un-negated mirror: it
        // matched the reference at r = -0.995 through the whole return pass,
        // perfectly shaped and exactly the wrong sign.
        v.ref += delta;
        if (v.ref > 0x7FFFF) v.ref = 0x7FFFF;
        if (v.ref < -0x80000) v.ref = -0x80000;
        --v.addr;
        if (v.addr <= v.smp.loop) {   // just produced the value at `loop`
            v.addr = v.smp.loop + 1;
            v.dir = 1;
        }
        return v.ref;
    }

    v.ref += delta;
    if (v.ref > 0x7FFFF) v.ref = 0x7FFFF;
    if (v.ref < -0x80000) v.ref = -0x80000;

    // A pure DPCM integrator need not return to the same value after a loop
    // pass. In practice the factory loops are authored exactly balanced -- the
    // drift is precisely zero for every sample checked -- but the snapshot
    // costs nothing and keeps a forward loop from walking into the clamp.
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
        if (v.smp.bidir) {
            // Turn around rather than jump back. The endpoints are played once
            // per half cycle, so the period is 2*(end-loop) and the waveform
            // never repeats within one traverse.
            v.addr = v.smp.end;
            v.dir = -1;
        } else {
            v.addr = v.smp.loop;
            if (v.loopSeen) v.ref = v.refAtLoop;
        }
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
    // Both coefficients come from the chip's own registers now, not from a
    // frequency and a fitted damping. The cutoff parameter -- envelope and
    // modulation already in it -- indexes the measured f table directly.
    v.coefF = tvfCoefF(cv);
    float res = v.resonance + v.resMod;
    if (res < 0.0f) res = 0.0f; else if (res > 127.0f) res = 127.0f;
    // HARD mode doubles the exponent of the damping law, which is what the
    // measured 0 / 2.8 / 5.1 / 8.3 / 12.1 dB of extra peak comes to.
    v.coefQ = JV_TVF_DAMPING(res * (v.resoHard ? JV_TVF_RESO_HARD_MUL : 1.0f));
    if (v.coefQ < 0.08f) v.coefQ = 0.08f;
    // At resonance 0 alone the chip adds damping at low cutoffs, its register
    // sliding 74 down to 64 across cutoffs 0 to 64 and flat above.
    if (res < 1.0f) {
        const float extra = (74.0f - 0.15625f * (cv < 0.0f ? 0.0f : cv)) * (1.0f / 64.0f);
        if (extra > v.coefQ) v.coefQ = (extra > 1.15625f) ? 1.15625f : extra;
    }
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
    if (v.portaCents != 0.0f) {
        cents += v.portaCents;
        v.portaCents -= v.portaStep;
        // Stop exactly on zero rather than overshooting into a detune.
        if ((v.portaStep > 0.0f && v.portaCents < 0.0f) ||
            (v.portaStep < 0.0f && v.portaCents > 0.0f)) v.portaCents = 0.0f;
    }
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

void Engine::startVoice(Voice& v, int toneIndex, uint8_t note, uint8_t vel,
                        int fromNote) {
    const uint8_t* t = patch_ + JV_TONE_OFFSET + toneIndex * JV_TONE_SIZE;
    Sample s;
    if (!sampleFor(t[1], note, s)) { v.active = false; return; }

    v.active = true;
    v.note = note;
    v.velocity = vel;
    v.tone = (uint8_t)toneIndex;
    v.smp = s;
    // A reverse sample starts at the far end and walks down to `start`. It has
    // to be `end` and not the loop point: the decoder is a differential
    // integrator started at zero, so a backward walk from address A puts out
    // v(A) - v(n) and carries v(A) as DC. This sample's deltas sum to exactly
    // zero over the body, which makes v(end) exactly 0 and v(loop) 1584 against
    // an RMS of 3865 -- starting at the loop point measures 88-99 % DC at the
    // output where the reference has 8-11 %. See tools/jv_extract/README.md.
    v.addr = s.reverse ? s.end : s.start;
    v.phase = 0;
    v.ref = 0;
    v.refAtLoop = 0;
    v.loopSeen = false;
    v.dir = s.reverse ? -1 : 1;
    v.sm1 = v.s0 = v.s1 = v.s2 = 0;
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

    // Velocity: the curve comes out of the ROM, not out of a fitted formula.
    // The machine holds seven of them at rom2:0x5390, 128 bytes each, indexed
    // by the raw MIDI velocity and selected by the low three bits of +71 --
    // located by logging which addresses the firmware actually reads during a
    // note-on. What they hold is not a warped velocity but a FALLING
    // attenuation index: curve 0 is exactly 254 - 2*velocity.
    //
    // The sensitivity law downstream is a function of sens * (127 - velA), and
    // curve 0 gives 254 - 2*vel = 2*(127 - vel) -- so the index the law wants
    // is simply half the table byte. That also settles why curve 0 always
    // looked right: the law was calibrated on it.
    //
    // Proven independent of how the byte arose: eight pairs of (curve,
    // velocity) that land on the same byte from different curves all produce
    // the same level, within 0.28 dB. So the attenuation is a function of the
    // byte alone, and feeding it through the existing law fits all seven
    // curves to 0.65 dB median, 2.2 dB worst -- against 6.7 to 20.9 dB before.
    const int velA = velocityIndex(t[71] & 7, vel);
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

    // The output section splits the panned signal three ways. Dry level scales
    // the direct path on its own; 31 of 539 factory tones set it below 127 and
    // six sit at or near zero, which on the hardware means they are heard only
    // through the effects. The sends are taken after the pan, as the manual's
    // block diagram has it -- level and panning sit inside the TVA, ahead of
    // the output section.
    v.dryGain = levelToLinear(t[81]);
    v.choGain = levelToLinear(t[83]);
    v.revGain = levelToLinear(t[82]);

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
    v.resoHard = (t[53] & JV_RESO_MODE_HARD_BIT) != 0;
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
    // Tone delay. NORMAL and the unused HOLD both just wait; PLAY-MATE takes
    // the gap since the previous note-on instead, scaled so that a parameter of
    // 64 reproduces it and 127 roughly doubles it, as the manual describes.
    {
        int dv = t[69];
        if (dv > JV_TONE_DELAY_MAX) dv = JV_TONE_DELAY_MAX;   // KEY-OFF, see header
        float ms = JV_TONE_DELAY_MS(dv);
        if (((t[71] >> 3) & 3) == JV_TONE_DELAY_PLAYMATE) {
            const float gapMs = haveLastNoteOn_
                ? (float)(clock_ - lastNoteOn_) * 1000.0f / (float)sr_ : 0.0f;
            ms = gapMs * ((float)dv / 64.0f);
            if (ms > 2000.0f) ms = 2000.0f;
        }
        v.delayRemaining = (int)(ms * 0.001f * (float)sr_ + 0.5f);
    }

    beginGlide(v, fromNote, note);
    updateModulation(v, clock_);
    updateFilterCoeffs(v);
}

// Sets up the portamento ramp for a voice that has just been tuned to `toNote`.
// The offset starts at the interval from the note being left and walks to zero,
// linearly in cents. TIME holds the duration constant whatever the interval;
// RATE holds the speed constant, so the duration scales with it.
void Engine::beginGlide(Voice& v, int fromNote, uint8_t toNote) {
    v.portaCents = 0.0f;
    v.portaStep = 0.0f;
    if (!portaOn() || fromNote < 0 || fromNote == (int)toNote) return;

    const float semis = (float)fromNote - (float)toNote;
    const float octMs = JV_PORTA_OCTAVE_MS(portaTime());
    float ms = octMs;
    if (patch_[25] & JV_PORTA_TYPE_RATE_BIT) ms *= fabsf(semis) / 12.0f;
    if (ms < 1.0f) return;

    const float ticks = ms * 0.001f * (float)sr_ / (float)kControlDiv;
    v.portaCents = semis * 100.0f;
    v.portaStep = v.portaCents / (ticks > 1.0f ? ticks : 1.0f);
}

// Retunes whatever is sounding to a new note without restarting it, which is
// what SOLO legato does: the sample keeps running and only the rate changes.
// The multisample zone is deliberately NOT re-selected -- that is the point of
// legato, and re-picking it would restart the sample.
bool Engine::retuneVoices(uint8_t note, int fromNote) {
    bool any = false;
    for (auto& v : voices_) {
        if (!v.active || v.tva.idle()) continue;
        const uint8_t* t = patch_ + JV_TONE_OFFSET + v.tone * JV_TONE_SIZE;
        const float pkf = jv_kf16(t[40] & 15) * 0.01f;
        const float effNote = 60.0f + ((float)note - 60.0f) * pkf;
        float semis = effNote - (float)v.smp.rootKey;
        semis += (float)(int8_t)t[37];
        semis += (float)(int8_t)t[38] * 0.01f;
        float cents = ((float)v.smp.tune - 1024.0f) * 0.1f + kMasterTuneCents;
        float ratio = powf(2.0f, semis * (1.0f / 12.0f) + cents * (1.0f / 1200.0f));
        ratio *= 32000.0f / (float)sr_ * pitchTrim_;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 8.0f) ratio = 8.0f;
        v.baseInc = (uint32_t)(ratio * 65536.0f);
        v.note = note;
        beginGlide(v, fromNote, note);
        any = true;
    }
    return any;
}

void Engine::noteOn(uint8_t note, uint8_t velocity) {
    if (!patch_) return;

    // Track held keys regardless of mode; SOLO needs the stack and POLY costs
    // nothing for it.
    if (heldN_ < (int)(sizeof held_)) held_[heldN_++] = note;

    const int from = soloNote_;
    const bool legato = soloMode() && heldN_ > 1 && from >= 0;

    if (soloMode()) {
        // Legato under SOLO keeps the sample running and only retunes, but only
        // when the patch asks for it. Portamento in LEGATO mode likewise glides
        // only when a key was already down.
        if (legato && soloLegato() && retuneVoices(note, from)) {
            soloNote_ = note;
            return;
        }
        for (auto& v : voices_) if (v.active) { v.tva.release(); v.tvf.release(); v.penv.release(); }
    }

    const int glideFrom = (!portaOn() || (portaLegatoOnly() && !legato)) ? -1 : from;
    soloNote_ = note;

    ++panAlt_;   // one step per note, so alternating tones swap sides
    // Patch common +12 bit 7: the patch velocity switch. See the window test
    // below -- it decides whether the per-tone velocity window applies at all.
    const bool velSwitch = (patch_[12] & 0x80) != 0;
    for (int tone = 0; tone < 4; tone++) {
        const uint8_t* t = patch_ + JV_TONE_OFFSET + tone * JV_TONE_SIZE;
        if (!(t[0] & 0x80)) continue;                        // tone switched off
        // Bytes +03/+04 are a velocity window, but a conditional one: it only
        // gates while bit 7 of the patch common byte -- the patch velocity
        // switch -- is set. That condition is what made the window look
        // disproven twice over. Gating unconditionally silenced six bank-A
        // basses below velocity 61 (St Fretless, House Bass, Thumpin Bass,
        // Pick Bass, Wonder Bass, Yowza Bass), every one of which carries
        // +03 = 61 with the switch CLEAR, so the machine ignores the window
        // and plays them; not gating at all sounded tones the machine keeps
        // silent, which is audible as a rattle under A20 Marimba SW, whose
        // third tone is windowed to 126..127 with the switch SET. Bank A
        // splits cleanly: all ten patches whose window must be ignored have
        // the switch clear, and all seventeen whose window must bite -- the
        // Rhodes family, SwitchOnMute, Velo Harmnix, Slap Bass, Marimba SW --
        // have it set. Measured on the reference: A20 at velocity 100 has no
        // high-frequency content at all (-35.2 dB against its fundamental)
        // and at velocity 127 it has +25.9 dB of it, the switched layer
        // coming in exactly as the window says.
        if (velSwitch && (velocity < t[3] || velocity > t[4])) continue;
        startVoice(voices_[allocVoice()], tone, note, velocity, glideFrom);
    }
    lastNoteOn_ = clock_;
    haveLastNoteOn_ = true;
}

void Engine::noteOff(uint8_t note) {
    // Drop it from the held stack wherever it sits -- keys are not always
    // released in the order they were pressed.
    for (int i = 0; i < heldN_; i++) {
        if (held_[i] == note) {
            for (int j = i; j + 1 < heldN_; j++) held_[j] = held_[j + 1];
            --heldN_;
            break;
        }
    }

    // Under SOLO, releasing the sounding note falls back to whatever is still
    // held rather than stopping -- that is what makes a trill under one finger
    // work. Only when nothing is left does the voice release.
    if (soloMode() && (int)note == soloNote_ && heldN_ > 0) {
        const uint8_t back = held_[heldN_ - 1];
        const int from = soloNote_;
        if (soloLegato() && retuneVoices(back, from)) { soloNote_ = back; return; }
        for (auto& v : voices_) if (v.active) { v.tva.release(); v.tvf.release(); v.penv.release(); }
        const int glideFrom = portaOn() ? from : -1;
        soloNote_ = back;
        ++panAlt_;
        for (int tone = 0; tone < 4; tone++) {
            const uint8_t* t = patch_ + JV_TONE_OFFSET + tone * JV_TONE_SIZE;
            if (!(t[0] & 0x80)) continue;
            startVoice(voices_[allocVoice()], tone, back, 100, glideFrom);
        }
        return;
    }

    if ((int)note == soloNote_) soloNote_ = -1;
    for (auto& v : voices_)
        if (v.active && v.note == note) { v.tva.release(); v.tvf.release(); v.penv.release(); }
}

void Engine::allNotesOff() {
    for (auto& v : voices_) v.active = false;
    heldN_ = 0;
    soloNote_ = -1;
    haveLastNoteOn_ = false;
}

int Engine::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) if (v.active) n++;
    return n;
}

// Chunked so the effect send buses can be fixed-size scratch rather than
// something sized by whatever the caller asks for.
void Engine::render(float* left, float* right, int frames) {
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (n > kRenderBlock) n = kRenderBlock;
        renderBlock(left + done, right + done, n);
        done += n;
    }
}

void Engine::renderBlock(float* left, float* right, int frames) {
    memset(left, 0, sizeof(float) * (size_t)frames);
    memset(right, 0, sizeof(float) * (size_t)frames);
    memset(choL_, 0, sizeof(float) * (size_t)frames);
    memset(choR_, 0, sizeof(float) * (size_t)frames);
    memset(revL_, 0, sizeof(float) * (size_t)frames);
    memset(revR_, 0, sizeof(float) * (size_t)frames);

    // The free-running phases run whether or not a voice is using them.
    for (int i = 0; i < 2; i++) {
        freePhase_[i] += freeInc_[i] * ((float)frames / (float)kControlDiv);
        freePhase_[i] -= (float)(int)freePhase_[i];
    }

    for (auto& v : voices_) {
        if (!v.active) continue;
        // A tone delay holds the whole voice off -- sample pointer, envelopes
        // and all -- rather than just muting it, so the sound starts from its
        // beginning when the wait is over.
        int first = 0;
        if (v.delayRemaining > 0) {
            if (v.delayRemaining >= frames) { v.delayRemaining -= frames; continue; }
            first = v.delayRemaining;
            v.delayRemaining = 0;
        }
        const bool filtered = (v.filtMode == JV_TVF_MODE_LPF || v.filtMode == JV_TVF_MODE_HPF);

        for (int i = first; i < frames; i++) {
            v.phase += v.inc;
            int steps = (int)(v.phase >> 16);
            v.phase &= 0xFFFFu;
            while (steps-- > 0) {
                v.sm1 = v.s0; v.s0 = v.s1; v.s1 = v.s2; v.s2 = decodeStep(v);
            }

            // 2^19 is the accumulator's full scale, but the ROM samples only
            // reach 13 % of it (median 7.7 %), so normalising to that throws
            // away 12 dB. This factor is what lines the engine up with the
            // reference. It has now moved twice for the same reason: an error
            // in the velocity law is indistinguishable from a level offset
            // unless the comparison is made at more than one velocity. It first
            // moved 10.5 dB when the law's direction was corrected, 1.0 dB
            // again when the velocity CURVES were measured, and 0.9 dB back the
            // other way once the chorus and reverb existed -- until then it had
            // been carrying their missing energy too. Fitted over all 128
            // factory patches at both velocities.
            const float* bw = kBSpline[v.phase >> 9];
            float s = ((float)v.sm1 * bw[0] + (float)v.s0 * bw[1] +
                       (float)v.s1  * bw[2] + (float)v.s2 * bw[3]) * (1.0f / 97867.0f);

            if (--v.ctlPhase <= 0) {
                updateModulation(v, clock_ + (uint32_t)i);
                if (filtered) updateFilterCoeffs(v);
                v.ctlPhase = kControlDiv;
            }
            if (filtered) {
                v.tvf.tick();
                s = v.filt.run(s, v.coefF, v.coefQ, v.filtMode);
            }

            const float a = v.tva.tick() * v.lfoGain;
            const float sl = s * a * v.gainL, sr = s * a * v.gainR;
            left[i]  += sl * v.dryGain;
            right[i] += sr * v.dryGain;
            choL_[i] += sl * v.choGain;
            choR_[i] += sr * v.choGain;
            revL_[i] += sl * v.revGain;
            revR_[i] += sr * v.revGain;
        }
        if (v.tva.idle()) v.active = false;
    }
    // The chorus either joins the mix or feeds the reverb, which is what bit 7
    // of its level byte selects. Ten factory patches route it that way.
    // CC91 / CC93 scale the whole send bus rather than the per-tone amounts,
    // which is what a channel-wide effect depth is.
    if (choScale_ != 1.0f)
        for (int i = 0; i < frames; i++) { choL_[i] *= choScale_; choR_[i] *= choScale_; }
    if (revScale_ != 1.0f)
        for (int i = 0; i < frames; i++) { revL_[i] *= revScale_; revR_[i] *= revScale_; }
    if (chorus_.toReverb) chorus_.process(choL_, choR_, revL_, revR_, frames);
    else                  chorus_.process(choL_, choR_, left, right, frames);
    reverb_.process(revL_, revR_, left, right, frames);
    clock_ += (uint32_t)frames;
}

} // namespace jv
