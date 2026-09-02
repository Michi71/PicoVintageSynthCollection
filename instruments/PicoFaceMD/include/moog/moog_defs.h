// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  PicoFaceMD -- Moog Minimoog Model D

  Rebuilt from the operation manual (doc/Minimoog-Manual.pdf,
  doc/Minimoog_Model_D_Manual.pdf) and the schematic in the service manual
  (doc/Minimoog_Service_Manual.pdf). The DSP models take their bearings from
  the two example projects named in the brief: the Krajeski/Huovilainen ladder
  of BelaMiniMoogEmulation (public domain) and the block structure of
  grimtraveller/moogvst.

  Signal flow of the original (front panel left to right):

    CONTROLLERS      Tune, Glide, Modulation Mix (osc 3 <-> noise),
                     oscillator modulation switch, osc 3 keyboard switch,
                     pitch and modulation wheel
    OSCILLATOR BANK  three oscillators, each Range (LO/32'/16'/8'/4'/2')
                     and Waveform (6 positions); osc 2 and 3 additionally a
                     Frequency control of +/- 7 semitones
    MIXER            volume plus on/off for osc 1..3, noise (white/pink) and
                     the external input -- which on the Model D is what the
                     output is patched back into for the famous feedback
    MODIFIERS        24 dB/oct transistor ladder low-pass with Cutoff,
                     Emphasis, Amount of Contour, filter modulation switch and
                     two keyboard tracking switches; one contour generator
                     (attack/decay/sustain) each for filter and loudness
    OUTPUT           main volume, A-440 tuning tone

  The Model D is monophonic. The keyboard produces one control voltage, and
  the manual is explicit about what happens with more than one key: "If more
  than one key is held down, only the lowest one has effect." That is modelled
  here rather than quietly turned into a polyphonic synthesiser -- the note
  priority and the single/multiple trigger behaviour are a large part of how
  the instrument plays.
*/

#ifndef MOOG_DEFS_H
#define MOOG_DEFS_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Host/target switch (same approach as PicoFaceCP / PicoFaceYC)             */
/* ------------------------------------------------------------------------ */
#ifdef MOOG_HOST_BUILD
#ifndef PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL
#define PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL 3
#endif
#ifndef SAMPLES_PER_BUFFER
#define SAMPLES_PER_BUFFER 16
#endif
#else
#include "audio_subsystem.h"
#endif

#define I2S_BUFFERS         PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL
#define I2S_BUFFER_WORDS    SAMPLES_PER_BUFFER
#define SAMPLING_RATE       (44100)

#define MOOG_BLOCK          I2S_BUFFER_WORDS

/* ------------------------------------------------------------------------ */
/* Oversampling                                                              */
/*                                                                           */
/* The oscillators are band-limited by polyBLEP, so they alias very little on */
/* their own. The aliasing that matters comes from the two saturating stages: */
/* the overdriven mixer and the tanh in the ladder. Both are soft, so their   */
/* harmonics decay quickly and 2x is enough -- 4x costs twice the CPU for a   */
/* difference that does not show up in a spectrum plot of a held note.        */
/*                                                                           */
/* Whatever is left above 22 kHz is taken out by a 6th order Butterworth at   */
/* 15 kHz before decimation (moog_voice.cpp). 15 kHz is not a compromise      */
/* here: the audio path of the original rolls off in the same region, and the */
/* filter doubles as the "vintage bandwidth" the sound is expected to have.   */
/* ------------------------------------------------------------------------ */
#ifndef MOOG_OVERSAMPLE
#define MOOG_OVERSAMPLE 2
#endif

#define MOOG_DECIMATE_HZ 15000.0f

/* ------------------------------------------------------------------------ */
/* Keyboard                                                                  */
/*                                                                           */
/* The original has 44 keys, F1..C5 (MIDI 29..72) -- the service manual gives */
/* the span as "(LO F) (HI C) 0 TO 3.64V" over a string of 43 resistors       */
/* (Fig. 9-2, Dwg 1436). The keyboard runs at 1.02 V/oct into a 102K summing  */
/* resistor, which is 1 V/oct exactly by the time the oscillators see it.     */
/*                                                                            */
/* The two constants below record that span; they do not gate anything. Notes */
/* outside it play where they are asked to, because a MIDI instrument with    */
/* dead zones at both ends of a 61-key controller would be worse, not more    */
/* faithful.                                                                  */
/* ------------------------------------------------------------------------ */
#define MOOG_KEY_FIRST      29      /* F1 */
#define MOOG_KEY_LAST       72      /* C5 -- 44 keys */

/* Held keys tracked for note priority. The Model D has one voice; this is
 * only the size of the list the priority rule picks from. */
#define MOOG_MAX_HELD_KEYS  16

/* Reference note the keyboard control voltage is measured against. Filter
 * tracking and the free-running oscillator 3 both hang off this. */
#define MOOG_CENTER_NOTE    60      /* C4 */

/* ------------------------------------------------------------------------ */
/* Oscillator bank                                                           */
/*                                                                           */
/* Range switch: LO, 32', 16', 8', 4', 2'. 8' is the note as played, every    */
/* further position one octave. LO sits far below 32' -- the manual describes */
/* it as "sub-audio clicks", and with the osc 3 keyboard switch off it is the */
/* instrument's only LFO.                                                     */
/*                                                                            */
/* How far below is in the schematic rather than the prose: the range switch  */
/* taps a resistor chain of 1K per octave (2'-4'-8'-16'-32'), and from 32'    */
/* down to LO sits a single 4.75K (Fig. 9-2). LO is therefore 4.75 octaves    */
/* under 32' and 6.75 under 8' -- middle C comes out at 2.4 Hz, which is the  */
/* usable end of an LFO. The -8.0 this used to hold put it at 1.0 Hz, too      */
/* slow across the whole keyboard.                                            */
/* ------------------------------------------------------------------------ */
#define MOOG_RANGE_COUNT    6
#define MOOG_LO_OCTAVES    (-6.75f) /* LO, relative to 8' (32' - 4.75K/1K) */

/* Waveform switch, six positions. Oscillator 3 substitutes a reverse
 * sawtooth for the sawtooth-triangular of oscillators 1 and 2. */
#define MOOG_WAVE_COUNT     6

/* Pulse widths of the three rectangular positions -- square, wide
 * rectangular, narrow rectangular.
 *
 * The service manual pins all three down. A panel divider of 1.5K / 1K / 7.5K
 * across -10 V (Dwg 1448) offers three tap voltages, 0 V, -1.5 V and -2.5 V,
 * and the oscillator board is calibrated against two of them: "0V = RECT" and
 * "-2.5V = 15 % D/CY" at the PW input (Fig. 9-2). The transfer between them is
 * linear -- a +/-1.75 V triangle against a threshold halved by a 100K/100K
 * divider -- so the middle tap lands on 29 %.
 *
 * The square really is square: note 4 of Fig. 9-1 has the timing components
 * "FACTORY SELECTED (IF NECESSARY) TO ACHIEVE 50 % +/-1 % RECT. DUTY CYCLE",
 * and Fig. 9-3 marks the waveform 50 %. An earlier 0.48 here was asserted to
 * be a measured asymmetry; it is outside the factory tolerance, and it puts a
 * second harmonic 24 dB down on the one waveform whose character is having
 * none. */
#define MOOG_PULSE_SQUARE   0.50f
#define MOOG_PULSE_WIDE     0.29f
#define MOOG_PULSE_NARROW   0.15f

/* The sawtooth-triangular, the "shark tooth", is not a waveform of its own.
 * R030 47K and R031 10K are strung between the oscillator's sawtooth and
 * triangle outputs and the switch taps their junction (Dwg 1448), so position
 * two carries a resistive mix of the other two:
 *
 *     V = (Vsaw/47K + Vtri/10K) / (1/47K + 1/10K)
 *       = (10*Vsaw + 47*Vtri) / 57
 *
 * Two consequences, and both are audible. It is mostly triangle, a sixth of
 * it sawtooth -- and because that sixth is a sawtooth, the mix carries a real
 * step at the end of every cycle, 0.35 of full scale. This engine used to
 * model the shape as a triangle with an off-centre peak, which is continuous
 * and therefore softer than the original.
 *
 * The polarity of the triangle shaper relative to the sawtooth is not legible
 * on the scan. It does not matter: the two possibilities are time-reverses of
 * one another, so they carry the same magnitude spectrum.
 *
 * Knowingly left out: at the junction the source impedance is 47K||10K, about
 * 8K, where the sawtooth and triangle positions are driven from followers.
 * The original's shark tooth is therefore also a little quieter than its
 * neighbours by an amount that moves with the volume pot -- a couple of dB
 * that would cost a setting-dependent gain to reproduce. */
#define MOOG_TRISAW_SAW    (10.0f / 57.0f)   /* 0.1754 */
#define MOOG_TRISAW_TRI    (47.0f / 57.0f)   /* 0.8246 */

/* Frequency control of oscillators 2 and 3. The panel is marked -7..+7 and
 * the schematic agrees: "7.5V +/-2.5V, +/-5TH(+)" at both controls, a fifth
 * either way (Fig. 9-2).
 *
 * With the oscillator 3 keyboard switch off the same control widens out. The
 * owner's manual rounds that to "a frequency sweep of 6 octaves"; the
 * schematic is more exact and marks the OSC 3 CONTROL line "+/-2.75 OCT". */
#define MOOG_FREQ_SEMITONES 7.0f
#define MOOG_FREQ_OCTAVES   2.75f

/* Master tune, in semitones either way. */
#define MOOG_TUNE_SEMITONES 2.5f

/* ------------------------------------------------------------------------ */
/* Vintage character                                                         */
/*                                                                           */
/* Three discrete oscillators on a warm circuit board never sit exactly where */
/* they are put. Each one gets a slow random walk of its own plus a fixed     */
/* offset drawn at power-up, which is what keeps two oscillators on the same  */
/* setting from cancelling into a static, lifeless tone. Depth in cents at    */
/* the maximum setting of the Drift control.                                  */
/* ------------------------------------------------------------------------ */
#define MOOG_DRIFT_CENTS    6.0f
#define MOOG_DRIFT_HZ       0.11f   /* corner of the random walk filter */
#define MOOG_TUNE_ERROR_CT  3.0f    /* fixed per-oscillator error, cents */

/* Cutoff of the ladder wanders as well, though far less than the pitch. */
#define MOOG_FILTER_DRIFT_CT 25.0f

/* Noise floor of the audio path, well below anything played but enough to
 * keep the output from being mathematically silent between notes. */
#define MOOG_HISS_LEVEL     4.0e-5f

/* Noise source: three outputs, not two.
 *
 * The noise board (Dwg 1431) is a chain -- generator, then a "-3db/OCTAVE
 * FILTER" giving pink, then a "100 Hz LOWPASS FILTER" giving a third output
 * the drawing calls red. All three are specified at -4 dBm, so each stage
 * makes back the level its filter takes out.
 *
 * The panel switch SW14 is double-pole and moves both taps at once: the audio
 * mixer gets white or pink, and the modulation mix gets pink or red -- always
 * one stage darker than what is being heard. The modulation mix amplifier
 * (Dwg 1444) has its noise input labelled "PINK OR RED NOISE", which is what
 * gives that away. It matters: red is what makes noise pointed at the filter
 * a slow random wander rather than a fizz.
 *
 * The order of the low-pass is not legible on the scan, but the network
 * carries two capacitors, so it is modelled with two poles at the corner the
 * drawing names. The gain is measured rather than derived -- it is what puts
 * red at the same rms as white, which is what -4 dBm on all three outputs
 * says the original does. */
#define MOOG_RED_HZ        100.0f
#define MOOG_RED_GAIN       2.07f   /* measured: puts red at the rms of white */

/* The noise channel of the mixer is not on the same footing as the other
 * four. Its series resistor into the summing node is R49 12K where the
 * oscillators and the external input all get 33K (Dwg 1446), so the channel
 * runs 2.75x hotter; against that, the noise source is specified at -4 dBm
 * (0.49 V rms, Dwg 1431) where an oscillator delivers 3.5 V p-p (1.01 V rms
 * on a sawtooth), which is 6.3 dB the other way. What is left is the factor
 * below -- about 2.5 dB, small, but it is the difference between noise that
 * sits under a patch and noise that is part of it. Both sources come out of
 * this engine at the same rms, so it applies as a plain channel gain. */
#define MOOG_NOISE_MIX_GAIN 1.33f

/* ------------------------------------------------------------------------ */
/* Ladder filter                                                             */
/*                                                                           */
/* Cutoff range of the panel: the Cutoff control is marked -4..+4 and covers  */
/* roughly ten octaves, from below the audio band to above it.                */
/* ------------------------------------------------------------------------ */
#define MOOG_CUTOFF_MIN_HZ    16.0f
#define MOOG_CUTOFF_MAX_HZ 16000.0f

/* Ceiling on the cutoff the ladder itself will accept, as a fraction of the
 * rate it runs at: one radian per sample, 1/2pi. Everything above the panel
 * range -- Contour, keyboard tracking, the modulation mix -- adds octaves on
 * top of the panel setting, so the filter is asked for cutoffs far past
 * MOOG_CUTOFF_MAX_HZ in ordinary playing, and past this point the Huovilainen
 * fits in moog_ladder.h stop being fits and start being wrong. See the note
 * on setCutoff(). */
#define MOOG_LADDER_WC_MAX_OVER_2PI 0.15915f

/* Emphasis 0..10. The manual: "When the EMPHASIS control is set to 10, the
 * filter breaks into oscillation, and produces a pure sine wave tone." The
 * model self-oscillates just above 1.0, so the top of the control has to
 * reach past it. */
#define MOOG_RESONANCE_MAX    1.06f

/* Amount of Contour at maximum, in octaves of cutoff sweep.
 *
 * Derived rather than guessed. The filter contour swings +0.1 V to +4.0 V
 * (Dwg 1437) and reaches the filter's control node through the 5K Amount pot
 * and R601 47K (Dwg 1446): 3.9 V / 47K = 83 uA. The same node takes the
 * keyboard at 1.02 V/oct through 100K, so one octave is 10.2 uA -- and the
 * contour is worth a little over eight of them. Six octaves, which this held
 * before, is a third short of what makes the filter envelope of a Model D
 * sound the way it does. */
#define MOOG_CONTOUR_OCTAVES  8.1f

/* Keyboard control switches. The manual: switch K "couples a small amount",
 * switch L "a larger amount", and with both on "the filter cutoff frequency
 * moves in full response to the keyboard control signal" -- so a third, two
 * thirds, and together the full 1 V/oct. */
#define MOOG_KBTRACK_1        (1.0f / 3.0f)
#define MOOG_KBTRACK_2        (2.0f / 3.0f)

/* ------------------------------------------------------------------------ */
/* Contour generators                                                        */
/*                                                                           */
/* The manual gives the attack as "as short as 10 milliseconds or as long as  */
/* 10 seconds", and the decay range as "about the same".                      */
/*                                                                           */
/* With the DECAY switch off the contour has effectively no release; with it  */
/* on the release takes the decay time. That switch is the reason a Model D   */
/* can play both a tight bass line and a long pad without touching anything   */
/* else.                                                                      */
/* ------------------------------------------------------------------------ */
#define MOOG_ENV_MIN_S      0.010f
#define MOOG_ENV_MAX_S     10.000f
#define MOOG_ENV_FAST_REL_S 0.006f  /* DECAY switch off */

/* An RC network charges towards a voltage above the one it is asked to
 * reach, which is what gives an analogue attack its shape -- fast at first,
 * easing in at the top. Charging towards 1.2 and stopping at 1.0 puts the
 * knee where the original has it. */
#define MOOG_ENV_OVERSHOOT  1.20f

/* ------------------------------------------------------------------------ */
/* Glide                                                                     */
/*                                                                           */
/* A 5 MOhm audio-taper pot charging C102, 1 uF (Dwg 1436) -- a time constant */
/* of five seconds at the top of the control. The number below is the time to */
/* cover 90 % of the interval, which is 2.3 time constants, so 11.5 s. The    */
/* 3.0 that stood here was roughly four times too fast; the squared panel law */
/* in moog_voice.cpp stands in for the audio taper of the original pot.       */
/* ------------------------------------------------------------------------ */
#define MOOG_GLIDE_MAX_S   11.5f

/* ------------------------------------------------------------------------ */
/* Output stage                                                              */
/* ------------------------------------------------------------------------ */
#define MOOG_DC_BLOCK_HZ    12.0f
#define MOOG_TONE_MIN_HZ  3000.0f
#define MOOG_TONE_MAX_HZ 18000.0f
#define MOOG_A440_HZ       440.0f
#define MOOG_A440_LEVEL      0.18f

/* Pitch bend. The manual: "as much as half an octave up or down". That is
 * the default; the panel value is adjustable because a MIDI controller
 * usually expects two semitones. */
#define MOOG_BEND_MAX_SEMITONES 12.0f

/* ------------------------------------------------------------------------ */
/* Effects                                                                   */
/*                                                                           */
/* A Model D has none of these. They sit behind the output stage as a section */
/* of their own, off in every factory preset but one, so the dry instrument    */
/* default and nothing here can change how it sounds until it is switched on. */
/*                                                                           */
/* Two slots, each of which may be empty or hold one of the three effects.    */
/* That is what bounds the cost: the worst pair measured is chorus and reverb */
/* together, and there is exactly one instance of each effect, so the same    */
/* one cannot end up in both slots.                                          */
/*                                                                           */
/* They are also what makes the instrument stereo. The voice is mono, as the  */
/* original is; the chorus and the reverb are the only things in the signal   */
/* path that produce a stereo image at all.                                   */
/* ------------------------------------------------------------------------ */

/* Chorus: three modulated lines, as in the ensemble of PicoFaceSM. */
#define MOOG_CHORUS_MAX_MS    40.0f
#define MOOG_CHORUS_BASE_MS   14.0f
#define MOOG_CHORUS_SWING_MS   7.0f
#define MOOG_CHORUS_HZ_MIN     0.10f
#define MOOG_CHORUS_HZ_MAX     4.00f
#define MOOG_CHORUS_LINES      3

/* Delay: one mono line with two output taps. A mono line rather than two is
 * what keeps the reverb affordable alongside it -- 750 ms stereo would cost
 * twice this. The right tap reads 8 % earlier than the left, which widens
 * the repeats without moving them off the beat. */
#define MOOG_DELAY_MAX_MS    750.0f
#define MOOG_DELAY_MIN_MS     30.0f
#define MOOG_DELAY_SPREAD      0.92f
#define MOOG_DELAY_FB_MAX      0.92f
#define MOOG_DELAY_TONE_MIN  800.0f
#define MOOG_DELAY_TONE_MAX 12000.0f

/* Reverb: the Schroeder/Freeverb arrangement, eight comb filters and four
 * all-passes per channel. The tunings are the usual ones, in samples at
 * 44.1 kHz. */
#define MOOG_REVERB_COMBS      8
#define MOOG_REVERB_ALLPASS    4
#define MOOG_REVERB_SPREAD    23      /* right channel offset, samples */
#define MOOG_REVERB_FB_MIN     0.70f
#define MOOG_REVERB_FB_MAX     0.98f

/* ------------------------------------------------------------------------ */
/* Tuning                                                                    */
/* ------------------------------------------------------------------------ */
#define MOOG_A4_HZ          440.0f
/* Frequency of MIDI note 0 (C-1) */
#define MOOG_NOTE0_HZ       8.1757989157f

#endif /* MOOG_DEFS_H */
