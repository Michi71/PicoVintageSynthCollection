// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_osc.h -- one of the three oscillators of the bank

  Six waveforms on a rotary switch, six ranges on another, and for
  oscillators 2 and 3 a frequency control on top. What comes out is a single
  band-limited sample per call.

  Aliasing is dealt with per waveform rather than by brute force:

    sawtooth, reverse sawtooth, the three rectangles
        discontinuous in the sample itself -- polyBLEP at every edge

    triangle, sawtooth-triangular
        continuous, only the slope jumps. Their harmonics fall off at 1/n^2,
        so the eleventh harmonic of a note at the top of the keyboard is
        already 40 dB down. Generated directly: a polyBLAMP would cost real
        cycles to correct something that sits under the noise floor of the
        original.

  Header-only on purpose. process() is called three times per oversampled
  sample -- at 88.2 kHz that is half a million calls a second, and a function
  call around eight lines of arithmetic would be most of the cost.

  The oscillator has no idea about drift or glide. It is told a frequency in
  Hz once per block and produces samples; everything that modulates that
  frequency lives in the voice.
*/

#ifndef MOOG_OSC_H
#define MOOG_OSC_H

#include "moog_defs.h"
#include "moog_dsp.h"

/* Waveform switch positions. The last three are rectangles of decreasing
 * width; MOOG_W_ALT is position 2, which is the sawtooth-triangular on
 * oscillators 1 and 2 and a reverse sawtooth on oscillator 3. */
enum MoogWave {
    MOOG_W_TRIANGLE = 0,
    MOOG_W_ALT,
    MOOG_W_SAW,
    MOOG_W_SQUARE,
    MOOG_W_WIDE,
    MOOG_W_NARROW
};

/*
 * polyBLEP correction around a step discontinuity.
 *
 * t is the phase (0..1), dt the phase increment. Within one sample either
 * side of the edge the ideal step is replaced by the integral of a windowed
 * impulse, which takes the worst of the aliasing out for the price of two
 * comparisons on most samples.
 */
static inline float moogPolyBlep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

class MoogOsc
{
public:
    void init(float sampleRate, uint32_t seed, bool isOsc3)
    {
        sr_    = sampleRate;
        osc3_  = isOsc3;
        wave_  = MOOG_W_SAW;
        inc_   = 0.0f;
        /* Free-running from a phase of its own, so that three oscillators on
         * identical settings do not start out phase-locked. */
        phase_ = (float) (seed & 0xFFFFu) / 65536.0f;
    }

    void setSampleRate(float sampleRate) { sr_ = sampleRate; }

    /* Frequency in Hz, applied from the next sample on. Clamped just short of
     * Nyquist: polyBLEP has nothing left to correct above that, and a
     * negative or runaway increment would walk the phase out of range. */
    void setFrequency(float hz)
    {
        hz  = moogClamp(hz, 0.0f, sr_ * 0.49f);
        inc_ = hz / sr_;
    }

    /* The same thing without the division, for the modulation path: the voice
     * works out the unmodulated increment once per sample and scales it per
     * oversampled step. */
    void  setIncrement(float inc) { inc_ = moogClamp(inc, 0.0f, 0.49f); }
    float increment() const       { return inc_; }

    /* Position of the waveform switch, 0..5. */
    void setWave(int wave)
    {
        wave_ = (wave < 0) ? 0 : (wave >= MOOG_WAVE_COUNT ? MOOG_WAVE_COUNT - 1 : wave);
    }

    /* A Model D does not reset its oscillators when a key goes down, which is
     * part of why two notes of the same patch never sound quite identical.
     * Only used when the engine as a whole is reset. */
    void resetPhase(float phase) { phase_ = phase; }

    float process()
    {
        const float dt = inc_;
        const float t  = phase_;

        float out;

        switch (wave_) {
            case MOOG_W_TRIANGLE:
                /* Rises over the first half, falls over the second. */
                out = 1.0f - 4.0f * fabsf(t - 0.5f);
                break;

            case MOOG_W_ALT:
                if (osc3_) {
                    /* Reverse sawtooth: the sawtooth mirrored, so the same
                     * correction applies with the opposite sign. */
                    out = 1.0f - 2.0f * t;
                    out += moogPolyBlep(t, dt);
                } else {
                    /* Sawtooth-triangular, the "shark tooth": a long rising
                     * ramp and a short fall. Continuous, so no correction --
                     * only the slope steps, not the value. */
                    out = (t < MOOG_TRISAW_BREAK)
                            ? (t * (2.0f / MOOG_TRISAW_BREAK) - 1.0f)
                            : (1.0f - (t - MOOG_TRISAW_BREAK) *
                                      (2.0f / (1.0f - MOOG_TRISAW_BREAK)));
                }
                break;

            case MOOG_W_SAW:
                out = 2.0f * t - 1.0f;
                out -= moogPolyBlep(t, dt);
                break;

            default: {
                /* The three rectangles differ only in duty cycle. Two edges
                 * per cycle, so two corrections: one at the rising edge at
                 * phase 0, one at the falling edge at phase w. */
                const float w = (wave_ == MOOG_W_SQUARE) ? MOOG_PULSE_SQUARE
                              : (wave_ == MOOG_W_WIDE)   ? MOOG_PULSE_WIDE
                                                         : MOOG_PULSE_NARROW;
                out = (t < w) ? 1.0f : -1.0f;
                out += moogPolyBlep(t, dt);

                float t2 = t - w;
                if (t2 < 0.0f) t2 += 1.0f;
                out -= moogPolyBlep(t2, dt);

                /* A narrow pulse has a much smaller mean square than a
                 * square wave. Without this the waveform switch would double
                 * as a volume control. */
                out *= (wave_ == MOOG_W_SQUARE) ? 1.0f
                     : (wave_ == MOOG_W_WIDE)   ? 1.15f : 1.45f;
                break;
            }
        }

        phase_ += dt;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        return out;
    }

private:
    float phase_ = 0.0f;
    float inc_   = 0.0f;      /* phase increment per sample */
    int   wave_  = MOOG_W_SAW;
    bool  osc3_  = false;
    float sr_    = (float) SAMPLING_RATE;
};

#endif /* MOOG_OSC_H */
