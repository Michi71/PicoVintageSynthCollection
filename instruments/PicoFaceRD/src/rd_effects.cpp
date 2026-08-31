// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "rd_effects.h"
#include "ram_hot.h"
#include <cmath>

// One-pole low-pass coefficient (0..1) from cutoff freq
static inline float onePoleCoef(float fc, float sr)
{
    return 1.0f - expf(-6.2831853f * fc / sr);
}

// Fast tanh/tan approximations (ported from PicoFaceCP dsp_fastmath.h)
static inline float fastTanh(float x) {
    x = x > 3.0f ? 3.0f : (x < -3.0f ? -3.0f : x);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float fastTan(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return x * (945.0f - 105.0f * x2 + x4) / (945.0f - 420.0f * x2 + 15.0f * x4);
}

// RAM-friendly 2^y: exp2f lives in flash, so calling it from the RAM-resident
// audio hot path triggers an XIP excursion. This cubic approx (max rel err
// ~2e-4) is inaudible for an LFO-swept cutoff; valid for y in roughly [-127,127].
static inline float fastExp2(float y)
{
    float n = floorf(y);
    float f = y - n;                                   // fraction in [0,1)
    float p = 1.0f + 0.695976f * f
                  + 0.224494f * (f * f)
                  + 0.0792024f * (f * f * f);
    union { uint32_t u; float fl; } v;
    v.u = (((uint32_t)(int32_t)n) + 127u) << 23;        // IEEE-754 exponent = 2^n
    return v.fl * p;
}

// Init all states, zero delay, then configure coefficients.
// Parameter defaults come from the member initializers in rd_effects.h.
void RD_VintageFX::init(float sampleRate)
{
    // All filter states zero
    dacLpState_   = 0.0f;
    dacLpState2_  = 0.0f;
    dacLpCoef_    = 0.0f;
    bassLpState_  = 0.0f;
    bassCoef_     = 0.0f;
    trebleLpState_= 0.0f;
    trebleCoef_   = 0.0f;

    // BBD low-pass states
    bbdLpStateA_  = 0.0f;
    bbdLpStateB_  = 0.0f;
    bbdLpCoef_    = 0.0f;

    // Delay line
    writeIdx_ = 0;
    for (int i = 0; i < kDelayLen; ++i)
        delay_[i] = 0.0f;

    // LFO phases
    tremPhase_   = 0.0f;
    tremInc_     = 0.0f;
    chorusPhase_ = 0.0f;
    chorusInc_   = 0.0f;

    // Phaser
    phPhase_ = 0.0f;
    phInc_   = 0.0f;
    phRateHz_ = 0.0f;
    phA_     = 0.9f;
    phFb_    = 0.2f;
    phCnt_   = 0;
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < kPhaserStages; ++i) { phX1_[c][i] = 0.0f; phY1_[c][i] = 0.0f; }
        phLast_[c] = 0.0f;
    }

    // Shelf gains derived from current bass_/treble_ params
    bassGain_   = powf(10.0f, (bass_   - 0.5f) * 18.0f / 20.0f) - 1.0f;
    trebleGain_ = powf(10.0f, (treble_ - 0.5f) * 18.0f / 20.0f) - 1.0f;

    chorusBaseSamples_  = 0.0f;
    chorusModSamples_   = 0.0f;

    setSampleRate(sampleRate);
}


// Chorus LFO frequency for a 0..1 setting, from the service notes' CP3 table.
static inline float chorusLfoHz(float rate01)
{
    return rd_chorus_rate_hz(rate01);          // law lives in rd_params.h
}

// Sweep width in samples. The CP3 table gives the LFO amplitude as well as the
// period, and their ratio is constant at 3.98 mV/ms (+-9 % over all fifteen
// settings) -- a triangle of constant slope. So the amplitude is proportional
// to the period, and the sweep widens as the rate falls.
//
// What that buys is not width for its own sake. A constant slope on the BBD
// clock control is a constant rate of change of delay, which is a constant
// pitch deviation: the original holds its detune and only changes how fast it
// wobbles. The model here used to hold the delay swing constant instead, so
// the detune grew with the rate -- nearly none at the slow end, most at the
// fast end. That is the opposite character.
//
// The absolute width is NOT derivable from the notes: the table gives volts at
// the LFO, and the volts-to-delay law lives in the MN3101 clock oscillator,
// which the schematic does not break out. Proportionality is measured; the
// anchor is a judgement, and it has now been made twice.
//
// It first preserved whatever depth this engine already had, which measured
// 132 cents of peak detune at full depth and 67 at the shipped default of
// 50 %. Two thirds of a semitone is not a chorus, it is a vibrato, and Michael
// said so on hardware -- "sehr verstimmt, ich kann nicht glauben, dass das so
// in echt war". He is right, and the number that preserved it was never
// evidence, only inertia.
//
// 0.0007 puts full depth at about 31 cents and the default at 15. A BBD chorus
// of the period -- Juno, Dimension D -- sits around 10 to 30, so the whole
// range now lands inside that band with the default in the middle of it. Set by
// ear against what the part can plausibly do, not by the manuals, which cannot
// settle it. Easy to move if it wants moving.
static inline float chorusSweepSamples(float depth01, float rate01, float sr)
{
    static const float kRefHz = 3.042f;          // setting 8 of fifteen
    const float want = depth01 * 0.0007f * sr * (kRefHz / chorusLfoHz(rate01));

    // The swing cannot exceed the centre delay, and that is physics rather than
    // a guard: the delay of a BBD is stages / (2 * clock), so it is positive by
    // construction. A swing equal to the centre is already a 2:1 clock sweep,
    // about as far as an MN3007 chorus goes. Without this the delay goes
    // negative at the slow settings, the read index wraps to the far end of the
    // line, and the output is garbage rather than chorus.
    //
    // At full depth the proportional law therefore holds from the fast end down
    // to about 1.8 Hz and flattens below that. At the depth settings the factory
    // patches actually use it holds further down.
    const float centre = 0.005f * sr;
    return want < centre ? want : centre;
}

// Configure all rate/coef-dependent parameters
void RD_VintageFX::setSampleRate(float sr)
{
    sampleRate_ = sr;

    // DAC reconstruction filter: 9 kHz or 0.45*sr max
    dacLpCoef_ = onePoleCoef(fminf(6000.0f, 0.35f * sr), sr);  // vintage reconstruction: ~6 kHz, 2-pole (12 dB/oct) -> audibly softer/darker

    // Bass/treble shelf low-pass references
    bassCoef_   = onePoleCoef(100.0f,  sr);
    trebleCoef_ = onePoleCoef(3000.0f, sr);

    // BBD anti-alias / reconstruction: 6 kHz or 0.4*sr max
    bbdLpCoef_ = onePoleCoef(fminf(6000.0f, 0.4f * sr), sr);

    // Tremolo LFO: 0.5..8 Hz
    // Measured like the chorus: the CP4 table runs 2100 ms down to 130 ms, which
    // is 0.476..7.692 Hz. The 0.5..8.0 that stood here was up to 9.8 % out; the
    // measured fit halves that and hits both ends exactly. It also has to be
    // right now that the display prints the figure.
    tremInc_ = rd_trem_rate_hz(tremRate_) / sr;

    // Chorus LFO: 0.3..1.2 Hz
    // Measured, not guessed: the service notes tabulate the chorus LFO period at
    // CP3 for all fifteen settings, 2700 ms down to 175 ms. That is 0.370 to
    // 5.714 Hz, linear in the setting to within 3.7 %. The old 0.3..1.2 Hz was
    // nearly five times too slow at the top of the range.
    chorusInc_ = chorusLfoHz(chorusRate_) / sr;

    // Phaser rate mapping: 0.1..5 Hz over normalized 0..1
    phRateHz_ = rd_phaser_rate_hz(phaserRate_);
    phInc_    = phRateHz_ / sr;

    // Chorus delay centre + modulation depth in samples
    chorusBaseSamples_ = 0.005f * sr;
    chorusModSamples_  = chorusSweepSamples(chorusDepth_, chorusRate_, sr);

    // Clamp so interpolation stays within the delay buffer
    float maxDelay = (float)(kDelayLen - 4);
    if (chorusBaseSamples_ + chorusModSamples_ > maxDelay)
        chorusModSamples_ = maxDelay - chorusBaseSamples_;
    if (chorusModSamples_ < 0.0f)
        chorusModSamples_ = 0.0f;
}

// Parameter setter: v01 is 0..1 normalized
void RD_VintageFX::setParam(uint8_t id, float v01)
{
    switch (id)
    {
    case RD_PARAM_VOLUME:
        volume_ = v01;
        break;

    case RD_PARAM_CHORUS_ON:
        chorusOn_ = (v01 >= 0.5f) ? 1 : 0;
        break;

    case RD_PARAM_CHORUS_RATE:
        chorusRate_ = v01;
        chorusInc_  = chorusLfoHz(chorusRate_) / sampleRate_;
        // The sweep width follows the rate now, so it has to be recomputed here too.
        chorusModSamples_ = chorusSweepSamples(chorusDepth_, chorusRate_, sampleRate_);
        {
            float maxDelay = (float)(kDelayLen - 4);
            if (chorusBaseSamples_ + chorusModSamples_ > maxDelay)
                chorusModSamples_ = maxDelay - chorusBaseSamples_;
            if (chorusModSamples_ < 0.0f) chorusModSamples_ = 0.0f;
        }
        break;

    case RD_PARAM_CHORUS_DEPTH:
        chorusDepth_ = v01;
        chorusModSamples_ = chorusSweepSamples(chorusDepth_, chorusRate_, sampleRate_);
        {
            float maxDelay = (float)(kDelayLen - 4);
            if (chorusBaseSamples_ + chorusModSamples_ > maxDelay)
                chorusModSamples_ = maxDelay - chorusBaseSamples_;
            if (chorusModSamples_ < 0.0f)
                chorusModSamples_ = 0.0f;
        }
        break;

    case RD_PARAM_TREM_ON:
        tremOn_ = (v01 >= 0.5f) ? 1 : 0;
        break;

    case RD_PARAM_TREM_RATE:
        tremRate_ = v01;
        tremInc_  = rd_trem_rate_hz(tremRate_) / sampleRate_;
        break;

    case RD_PARAM_TREM_DEPTH:
        tremDepth_ = v01;
        break;

    case RD_PARAM_BASS:
        bass_     = v01;
        bassGain_ = powf(10.0f, (v01 - 0.5f) * 18.0f / 20.0f) - 1.0f;
        break;

    case RD_PARAM_TREBLE:
        treble_     = v01;
        trebleGain_ = powf(10.0f, (v01 - 0.5f) * 18.0f / 20.0f) - 1.0f;
        break;

    case RD_PARAM_DAC_FILTER_ON:
        dacOn_ = (v01 >= 0.5f) ? 1 : 0;
        break;

    case RD_PARAM_PHASER_ON:
        phaserOn_ = (v01 >= 0.5f) ? 1.0f : 0.0f;
        break;

    case RD_PARAM_PHASER_RATE:
        phaserRate_ = v01;
        phRateHz_   = rd_phaser_rate_hz(phaserRate_);
        phInc_      = phRateHz_ / sampleRate_;
        break;

    case RD_PARAM_PHASER_DEPTH:
        phaserDepth_ = v01;
        break;

    default:
        break;
    }
}

// Parabolic sine approximation, phase01 in 0..1, returns -1..1
float RD_VintageFX::sinApprox(float phase01)
{
    // Map to -pi..pi via triangle, then parabolic fit
    float x = phase01;
    x -= 0.5f;                 // -0.5 .. 0.5
    float s = (x >= 0.0f) ? 1.0f : -1.0f;
    float a = fabsf(x);        // 0 .. 0.5
    // Parabola: y = 8*a*(1-2*a), scaled by sign; peaks at a=0.25 -> +/-1.0
    return s * (8.0f * a * (1.0f - 2.0f * a));
}

void RAM_HOT(RD_VintageFX::process)(float in, float* outL, float* outR) {
    float x = in;

    // 1. Vintage DAC: 16-bit requantization + 2-pole reconstruction lowpass.
    //    FULLY gated -- bypassing must restore the clean signal (the old code
    //    ran the lowpass unconditionally, so the toggle did nothing audible).
    if (dacOn_ > 0.5f) {
        // 16 bit, not 12: the MKS-20 service notes list IC4 on the CPU-B board
        // as a PCM54 -- "16 bit D/A converter" in as many words. The 12 here was
        // a guess from before the schematic turned up, and it cost 24 dB of
        // quantization noise the machine never had.
        //
        // floorf(x+0.5) rather than a cast, too. A cast truncates toward zero,
        // which leaves a dead band of one LSB either side of silence -- crossover
        // distortion, worst exactly where a piano tail spends its time. This is a
        // plain mid-tread quantizer with no dead band.
        x = floorf(x * 32768.0f + 0.5f) * (1.0f / 32768.0f);
        dacLpState_  += dacLpCoef_ * (x - dacLpState_);
        dacLpState2_ += dacLpCoef_ * (dacLpState_ - dacLpState2_);
        x = dacLpState2_;
    }

    // 2. Bass shelf
    bassLpState_ += bassCoef_ * (x - bassLpState_);
    x += bassGain_ * bassLpState_;

    // 3. Treble shelf
    trebleLpState_ += trebleCoef_ * (x - trebleLpState_);
    x += trebleGain_ * (x - trebleLpState_);

    // 4. Chorus (BBD), 5. phaser, 6. tremolo -- in that order, which is the
    //    machines' own and the reverse of what stood here.
    //
    //    Both block diagrams run EQ -> chorus -> phaser -> tremolo/VCA -> out
    //    (MKS-20 service notes p.4, MK-80 p.17; the MKS-20 has no phaser, so its
    //    chain is chorus -> tremolo). The chain here ran tremolo -> phaser ->
    //    chorus, so the tremolo went THROUGH the delay line: smeared over five
    //    milliseconds and split unevenly across the two taps, where the original
    //    applies it last and cleanly.
    delay_[writeIdx_] = x;
    float l, r;
    if (chorusOn_ > 0.5f) {
        chorusPhase_ += chorusInc_;
        if (chorusPhase_ >= 1.0f) chorusPhase_ -= 1.0f;
        float phB = chorusPhase_ + 0.5f;
        if (phB >= 1.0f) phB -= 1.0f;

        float delayA = chorusBaseSamples_ + chorusModSamples_ * triangle(chorusPhase_);
        float delayB = chorusBaseSamples_ + chorusModSamples_ * triangle(phB);

        float readPosA = writeIdx_ - delayA;
        if (readPosA < 0.0f) readPosA += kDelayLen;
        int i0A = (int)readPosA;
        int i1A = (i0A + 1) & (kDelayLen - 1);
        float fracA = readPosA - i0A;
        float tapA = delay_[i0A] + fracA * (delay_[i1A] - delay_[i0A]);

        float readPosB = writeIdx_ - delayB;
        if (readPosB < 0.0f) readPosB += kDelayLen;
        int i0B = (int)readPosB;
        int i1B = (i0B + 1) & (kDelayLen - 1);
        float fracB = readPosB - i0B;
        float tapB = delay_[i0B] + fracB * (delay_[i1B] - delay_[i0B]);

        bbdLpStateA_ += bbdLpCoef_ * (tapA - bbdLpStateA_);
        bbdLpStateB_ += bbdLpCoef_ * (tapB - bbdLpStateB_);
        l = (x + bbdLpStateA_) * 0.7f;
        r = (x + bbdLpStateB_) * 0.7f;
    } else {
        l = x;
        r = x;
    }

    // 5. Phaser -- one per channel now, sharing the LFO. The MK-80 has two
    //    (IC33 twice on the effect board, each inside its own NE572 compander),
    //    and after the chorus the two sides are no longer the same signal, so a
    //    single mono phaser has nothing coherent to work on.
    if (phaserOn_ > 0.5f) {
        if (++phCnt_ >= 8u) {
            phCnt_ = 0u;
            float oct = 0.6f + 1.6f * phaserDepth_;
            float fc  = 750.0f * fastExp2(oct * sinApprox(phPhase_));
            float t   = fastTan(3.14159265358979323846f * fc / sampleRate_);
            phA_  = (1.0f - t) / (1.0f + t);
            phFb_ = 0.2f + 0.55f * phaserDepth_;
        }
        phPhase_ += phInc_;
        if (phPhase_ >= 1.0f) phPhase_ -= 1.0f;

        float* ch[2] = { &l, &r };
        for (int c = 0; c < 2; ++c) {
            float sig = *ch[c] + phFb_ * fastTanh(phLast_[c]);
            for (int i = 0; i < kPhaserStages; ++i) {
                float y = -phA_ * sig + phX1_[c][i] + phA_ * phY1_[c][i];
                phX1_[c][i] = sig;
                phY1_[c][i] = y;
                sig = y;
            }
            phLast_[c] = sig;
            *ch[c] = 0.5f * *ch[c] + 0.5f * sig;
        }
    }

    // 6. Tremolo -- stereo and antiphase, which is what it is on the machine
    //    rather than a stereo dressing. The MK-80 notes adjust the two VCAs
    //    separately (VR8 for OUTPUT-L, VR9 for OUTPUT-R) and their scope trace
    //    shows the two channels exactly interleaved: the right swells where the
    //    left is at its trough. At full depth it is a pan, not a level wobble.
    //
    //    The trough goes to silence, too. The instruction is to set it "at
    //    minimum level (possibly zero swing)", where the factor here used to
    //    stop at 0.2 of full scale.
    if (tremOn_ > 0.5f) {
        tremPhase_ += tremInc_;
        if (tremPhase_ >= 1.0f) tremPhase_ -= 1.0f;
        float pR = tremPhase_ + 0.5f;
        if (pR >= 1.0f) pR -= 1.0f;
        l *= 1.0f - tremDepth_ * (0.5f + 0.5f * sinApprox(tremPhase_));
        r *= 1.0f - tremDepth_ * (0.5f + 0.5f * sinApprox(pR));
    }

    *outL = l * volume_;
    *outR = r * volume_;

    writeIdx_ = (writeIdx_ + 1) & (kDelayLen - 1);
}

// Triangle wave, phase01 in 0..1, returns -1..1
float RD_VintageFX::triangle(float phase01)
{
    // 0->0, 0.25->1, 0.5->0, 0.75->-1, 1->0
    float p = phase01 - 0.25f;
    if (p < 0.0f) p += 1.0f;
    if (p >= 0.5f)
        return 3.0f - 4.0f * p;   // 0.5..1 -> 1..-1
    else
        return 4.0f * p - 1.0f;   // 0..0.5 -> -1..1
}
