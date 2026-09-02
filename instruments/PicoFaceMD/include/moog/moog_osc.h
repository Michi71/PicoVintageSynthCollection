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

    sawtooth-triangular
        mostly triangle, but the sixth of it that is sawtooth steps at the end
        of every cycle, so it takes a polyBLEP scaled to that share

    triangle
        continuous, only the slope jumps. Its harmonics fall off at 1/n^2, so
        the eleventh harmonic of a note at the top of the keyboard is already
        40 dB down. Generated directly: a polyBLAMP would cost real cycles to
        correct something that sits under the noise floor of the original.

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
                    /* Sawtooth-triangular, the "shark tooth": the resistive
                     * mix of this oscillator's own sawtooth and triangle that
                     * the panel taps between R030 and R031 -- see
                     * MOOG_TRISAW_SAW. Mostly triangle, with enough sawtooth
                     * in it to rise faster than it falls.
                     *
                     * The sawtooth share brings a step with it, so this needs
                     * the correction after all. Scaled by that share, because
                     * the step is the sawtooth's own and nothing else here
                     * jumps. */
                    out = MOOG_TRISAW_TRI * (1.0f - 4.0f * fabsf(t - 0.5f))
                        + MOOG_TRISAW_SAW * (2.0f * t - 1.0f);
                    out -= moogPolyBlep(t, dt) * MOOG_TRISAW_SAW;
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

                /* Coupled, as the 10 uF between the waveform switch and the
                 * mixer's audio node couples it (Fig. 9-2). Without this the
                 * rectangle arrives at the ladder sitting on its own duty
                 * cycle -- -0.42 for the wide one, -0.70 for the narrow --
                 * and biases the saturating input stage, which squashes one
                 * half of the wave against the other. The original's filter
                 * never sees that offset.
                 *
                 * It is also what makes the three positions differ in level
                 * at all. The comparator output is a logic swing and knows
                 * nothing about duty cycle: all three leave the oscillator
                 * board at the same 3.5 V peak to peak (Fig. 9-2 pin 20B,
                 * "0 / -3.5V"; Fig. 9-3 puts the triangle and sawtooth at
                 * +/-1.75 V, the same span). Uncoupled they would all carry
                 * the same mean square. Coupled, the mean square goes as
                 * 4*w*(1-w), so 29 % lands 0.8 dB under the square and 15 %
                 * lands 2.9 dB under it, and the peak turns asymmetric --
                 * a narrow pulse reaches +1.7 against -0.3, exactly as
                 * 2.975 V against -0.525 V on the instrument.
                 *
                 * So on a Model D the waveform switch really is a little bit
                 * of a volume control. This engine used to make that back
                 * with a gain per position, which is convenient and is not
                 * what the instrument does. */
                out -= 2.0f * w - 1.0f;
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
