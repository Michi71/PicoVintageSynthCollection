// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  PicoFaceJ6 -- Roland Juno-60

  Built from three sources, and where they disagree the order of precedence is
  stated at the value:

    doc/Roland Juno-60 Service Notes ( HI-RES ).pdf
        The specifications page and the schematics. Component values and IC
        types come from here and nothing overrides them.

    doc/PG-JU60_Manual_eng_05_W.pdf
        What the controls are called and what they are meant to do.

    https://github.com/pendragon-andyh/Juno60
        Measurements taken off a real instrument -- envelope times and slopes,
        chorus rates and delay ranges, HPF corner frequencies. Where a
        measurement contradicts the specification, the measurement wins and
        both numbers are recorded.

    https://github.com/dzannotti/junox
        Structure and parameter scaling. Two places where it is not followed
        are marked in the code: its DCO quantises the period to whole audio
        samples, which is not what the instrument does, and its filter is a
        diode ladder, which is not what an IR3109 is.

  Signal flow (service notes, block diagram, page 3):

    MASTER OSC -> DCO x6 -> WAVEFORM -> VCF x6 (IR3109) -> VCA x6 (BA662)
      -> SUM -> HPF (switched, one for all voices) -> VCA (patch level)
      -> CHORUS (2x MN3009 BBD) -> VOLUME -> output

  Two details of that chain are easy to get wrong and are worth stating:
  the high-pass filter sits after the voices are summed, not in each voice,
  and the patch's VCA level sits *before* the chorus, so a loud patch drives
  the bucket-brigade lines harder and distorts them more.
*/

#ifndef JUNO_DEFS_H
#define JUNO_DEFS_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Host/target switch (same approach as the rest of the family)              */
/* ------------------------------------------------------------------------ */
#ifdef JUNO_HOST_BUILD
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

#define JUNO_BLOCK          I2S_BUFFER_WORDS

/* ------------------------------------------------------------------------ */
/* Oversampling                                                              */
/*                                                                           */
/* Six voices cost six times what one does, so this matters far more here     */
/* than it did for the Model D, and it is the one number worth getting right   */
/* before anything else.                                                      */
/*                                                                           */
/* Measured on this engine, scaled against the load PicoFaceMD reports on the  */
/* device (its dry engine is 0.185 % of one host core and P21 on the RP2350):  */
/*                                                                           */
/*     1x   fixed P8, P6.2 per voice, chorus P2   -> six voices P48            */
/*     2x   fixed P10, P10 per voice, chorus P2   -> six voices P72            */
/*                                                                           */
/* A prototype written before the engine predicted P42 and P59. It was too     */
/* optimistic by 13 points at 2x, so the earlier conclusion that one core is   */
/* comfortable at 2x does not survive its own measurement: P72 leaves only a   */
/* quarter of the core for USB, MIDI and pushing the display out.              */
/*                                                                           */
/* Aliasing, one voice, sawtooth, filter wide open and no resonance -- the     */
/* only configuration that can be measured honestly, since a resonant peak     */
/* below the fundamental is not at a harmonic and swamps the figure:           */
/*                                                                           */
/*     1x  -45.2 dB     2x  -55.4 dB     4x  -53.7 dB                          */
/*                                                                           */
/* So 2x buys 10 dB and 4x buys nothing, the same shape of result as the       */
/* Model D. 1x is the default because it fits one core with room to spare;     */
/* going to 2x means either living at P72 or splitting the voices over both    */
/* cores the way PicoFaceRD does.                                             */
/* ------------------------------------------------------------------------ */
#ifndef JUNO_OVERSAMPLE
#define JUNO_OVERSAMPLE 1
#endif

#define JUNO_DECIMATE_HZ 15000.0f

/* ------------------------------------------------------------------------ */
/* Polyphony                                                                 */
/*                                                                           */
/* Six, fixed. The instrument has six voice cards -- the schematic shows six  */
/* IR3109 filters (IC2, 5, 8, 11, 14, 17) and six BA662 amplifiers -- and     */
/* what happens on the seventh note is part of how a Juno plays.              */
/* ------------------------------------------------------------------------ */
#define JUNO_VOICES         6

/* 61 keys, 5 octaves (specifications page). Notes outside that are accepted
 * anyway: the key CV is a voltage, and nothing in the circuit stops it going
 * past the ends of the instrument's own keyboard. */
#define JUNO_KEY_FIRST      36      /* C2 */
#define JUNO_KEY_LAST       96      /* C7 -- 61 keys */
#define JUNO_CENTER_NOTE    60      /* C4, reference for key follow */

/* ------------------------------------------------------------------------ */
/* DCO                                                                       */
/*                                                                           */
/* One per voice. The signal path is analogue but the timing comes from a     */
/* digital clock, which is why a Juno stays in tune in a way a VCO does not.  */
/* There is therefore no oscillator drift to model -- the opposite of the     */
/* Model D, where the drift was most of the character.                        */
/*                                                                           */
/* junox reproduces the digital clock as round(sampleRate / frequency), which */
/* quantises the period to whole audio samples. That is not what the          */
/* instrument does: its counter runs in the megahertz, so its quantisation    */
/* sits far below one audio sample. At 44.1 kHz junox's approach would put    */
/* the top octave tens of cents out. A float accumulator is used here, which  */
/* is both more accurate and cheaper.                                         */
/* ------------------------------------------------------------------------ */
#define JUNO_RANGE_COUNT    3       /* 16' / 8' / 4' */

/* Pulse width. The panel control runs from a square to a narrow pulse;
 * junox maps it to 0.5..0.95 duty, which is the same thing by symmetry. */
#define JUNO_PW_MIN         0.50f
#define JUNO_PW_MAX         0.95f

/* Sub-oscillator: a square one octave below the DCO. */
#define JUNO_SUB_OCTAVES   (-1.0f)

/* Noise. A note on the AR80017A filter clone says the noise source is
 * low-passed at 5 kHz, which is what keeps it from sounding like a hiss
 * generator bolted to the side. */
#define JUNO_NOISE_LP_HZ    5000.0f

/* Master tune, specifications page: +/- 50 cents. */
#define JUNO_TUNE_CENTS     50.0f

/* ------------------------------------------------------------------------ */
/* VCF -- IR3109                                                             */
/*                                                                           */
/* Four OTA integrator sections in series inside a feedback loop, with a      */
/* BA662 setting the amount of feedback. Structurally the same class as the    */
/* transistor ladder of the Model D: four one-poles and one saturating stage. */
/* Not a diode ladder, which is what junox models it as.                      */
/*                                                                           */
/* The audible difference from a Moog ladder is the bass. A transistor ladder */
/* loses low end as the resonance comes up because the feedback is taken from */
/* inside the ladder; an OTA cascade with a separate feedback amplifier does  */
/* not. That is the gComp term below, and it is set high for exactly this     */
/* reason -- a Juno with the resonance up is never thin.                      */
/*                                                                           */
/* Specifications page: RESONANCE (0 - Self Oscillation), KEY FOLLOW          */
/* (0 - 100%).                                                                */
/* ------------------------------------------------------------------------ */
#define JUNO_CUTOFF_MIN_HZ     20.0f
#define JUNO_CUTOFF_MAX_HZ  18000.0f
#define JUNO_RESONANCE_MAX      1.06f   /* self-oscillates a little above 1 */
#define JUNO_VCF_GCOMP          0.85f   /* Moog ladder uses 0.5; see above  */

/*
 * How far the contour can open the filter, at Env fully up.
 *
 * This was 6 octaves to begin with and that was simply wrong. A Juno patch
 * routinely leaves the Cutoff slider near zero and lets the contour do all of
 * the work -- the "Brass" patch has Cutoff at 0 and Env at 0.8, and "Piano I"
 * has 0.1 and 0.7. With only 6 octaves of contour, Brass opened to 557 Hz and
 * settled at 147: quiet, dull, and nothing like brass. Piano I swept down to
 * 39 Hz over several seconds, which is a good description of a ghost.
 *
 * The cutoff range itself spans log2(18000/20) = 9.8 octaves, and on the
 * instrument the contour at full can take the filter from shut to open. So the
 * contour has to cover essentially that whole range. junox uses 14 "octaves"
 * against a slider worth 11 of them, which is the same intent expressed
 * sloppily.
 */
#ifndef JUNO_CONTOUR_OCTAVES
#define JUNO_CONTOUR_OCTAVES   10.0f
#endif

#define JUNO_LFO_VCF_OCTAVES    3.0f

/* ------------------------------------------------------------------------ */
/* HPF                                                                       */
/*                                                                           */
/* One for all six voices, after the sum (service notes, block diagram). A    */
/* HD14051B switches between three capacitors (0.022 / 0.01 / 0.0047 uF) and  */
/* a bypass, so the control has exactly four positions and no values in       */
/* between.                                                                   */
/*                                                                           */
/* Corner frequencies from the Juno60 measurements, worked out from the       */
/* component values as F = 1/(2*pi*R*C). junox uses 0 / 250 / 520 / 1220 Hz   */
/* instead and says in a comment that it kept them because they sound good,   */
/* not because they are right; these are the ones that follow from the parts. */
/* 6 dB per octave -- a single pole.                                          */
/* ------------------------------------------------------------------------ */
#define JUNO_HPF_POSITIONS  4
#define JUNO_HPF_HZ_1       154.0f
#define JUNO_HPF_HZ_2       339.0f
#define JUNO_HPF_HZ_3       720.0f

/* ------------------------------------------------------------------------ */
/* Envelope -- IR3201                                                        */
/*                                                                           */
/* Times, from the specifications page:                                       */
/*     ATTACK  1 ms .. 3 s                                                    */
/*     DECAY   2 ms .. 12 s                                                   */
/*     RELEASE 2 ms .. 12 s                                                   */
/*                                                                           */
/* Measured off a real instrument (Juno60), which does not agree:             */
/*     ATTACK  slider 0/2.5/5/7.5/10 -> 0.001 / 0.03 / 0.24 / 0.65 / 3.25 s   */
/*     DECAY   slider 0/2.5/5/7.5/10 -> 0.002 / 0.096 / 0.984 / 4.449 /       */
/*             19.783 s                                                       */
/*                                                                           */
/* The measurement wins -- 19.8 s where the sheet says 12 is a big gap, but   */
/* the sheet also says the diagrams in the manual are not meant to be exact,  */
/* and a measurement off the actual hardware beats a round number in a table. */
/*                                                                           */
/* Two things about the shape matter more than the numbers:                    */
/*                                                                           */
/*   The decay duration does not depend on the sustain level. Measured with   */
/*   sustain at 0 and at 5, the times came out the same (19.78 s and 17.11 s  */
/*   at the top of the slider). That rules out the RC-charging-towards-target */
/*   model used in PicoFaceMD, where the time to reach sustain shortens as    */
/*   sustain rises. Segments here have a fixed duration.                       */
/*                                                                           */
/*   The slopes are curved, and junox interpolates them linearly. Measured    */
/*   attack at slider 10 reaches 0.224 after half a second of a 3.25 s rise,  */
/*   where a straight line would be at 0.154. Juno60 fits (1-e^-x)/0.632.     */
/* ------------------------------------------------------------------------ */
#define JUNO_ATTACK_MIN_S    0.001f
#define JUNO_ATTACK_MAX_S    3.250f
#define JUNO_ATTACK_CURVE    0.5f     /* exponent of the slider mapping      */
#define JUNO_ATTACK_SHAPE    0.632f   /* 1 - 1/e; normalises (1-e^-x)        */

#define JUNO_DECAY_MIN_S     0.002f
#define JUNO_DECAY_MAX_S    17.460f
#define JUNO_DECAY_CURVE     0.4f

/* Falling segments: level = target + (1-target) * e^(-k t/T). k = ln(100),
 * so the segment is within a percent of its target when it ends. Fitted
 * against the measured decay at slider 10 (1.000 / 0.764 / 0.616 / 0.511 at
 * 0/1/2/3 s of a 19.8 s fall) -- this reproduces 0.793 / 0.628 / 0.498,
 * within a few percent, where a straight line gives 0.949 / 0.899 / 0.848. */
#define JUNO_FALL_SHAPE      4.605f

/* Gate mode, measured: attack 3 ms, release 6 ms. Present to stop clicks
 * rather than to shape anything. */
#define JUNO_GATE_ATTACK_S   0.003f
#define JUNO_GATE_RELEASE_S  0.006f

/* ------------------------------------------------------------------------ */
/* LFO                                                                       */
/*                                                                           */
/* Specifications page: RATE 0.3 .. 20 Hz, DELAY TIME 0 .. 1.5 s.             */
/* Triangle. junox's rate mapping reproduces the range and puts the middle of */
/* the slider at 3.5 Hz, which is used here.                                  */
/*                                                                           */
/* Delay: measured (Juno60) as two things at once -- silence, then a ramp in. */
/* At the top of the slider that is 2.786 s of silence and a further second   */
/* of fade, which again overshoots the specified 1.5 s.                        */
/* ------------------------------------------------------------------------ */
/* The specifications page says 20 Hz; the factory adjustment is more exact
 * and sets the top of the slider to a 45 ms period, which Fig. 29 of the
 * service notes labels 22 Hz. */
#define JUNO_LFO_HZ_MIN      0.3f
#define JUNO_LFO_HZ_MAX     22.0f
#define JUNO_LFO_DELAY_MAX_S 2.786f
#define JUNO_LFO_FADE_MAX_S  1.000f
/*
 * Pitch modulation at full DCO LFO depth, in semitones.
 *
 * The service notes settle this outright. Describing the master oscillator
 * (TR58-TR62), which carries the common bender, LFO and tune voltages, they
 * give its variable range per input:
 *
 *     BENDER  +/-700 cents
 *     LFO     +/-300 cents
 *     TUNE    +/- 50 cents
 *
 * and check the arithmetic themselves -- "when these voltages are summed
 * together, the maximum shiftable range is +/-1050 cents". So a fully raised
 * DCO LFO slider is worth three semitones.
 *
 * This held 1.0 before, taken from junox by construction rather than from any
 * measurement, and it made the factory patches too timid to hear: at 1.0 the
 * fifteen that use the slider get between 10 and 40 cents of vibrato, where
 * a violin or an oboe wants three or four times that. Seven, which it held
 * before that, was a guess and was too much.
 */
#define JUNO_LFO_DCO_SEMIS   3.0f

/* ------------------------------------------------------------------------ */
/* Chorus -- 2x MN3009 BBD, MN3101 clocks                                    */
/*                                                                           */
/* One triangle LFO driving two bucket-brigade lines, one per channel, with   */
/* the right-hand modulation inverted so the two are half a cycle apart.      */
/*                                                                           */
/* Rates measured off the instrument (Juno60), which also showed that the     */
/* service notes have a typo: they label the third setting 1 Hz where it is   */
/* really 9.75 Hz. Without that correction the I+II setting would have been   */
/* built ten times too slow.                                                  */
/*                                                                           */
/* An MN3009 has 256 stages, so its delay is 256/(2*fclk). The measured       */
/* 1.66 .. 5.35 ms therefore puts the clock between about 77 kHz and 24 kHz.  */
/* At the long end the line's own Nyquist limit is down at 12 kHz, so the     */
/* top comes and goes with the modulation. That is the chip, not a fault, and */
/* it is modelled with a low-pass that follows the delay.                      */
/*                                                                           */
/* I+II is mono: both lines get the same modulation, so what comes out is a   */
/* vibrato rather than a chorus. The manual calls it Leslie-like.             */
/* ------------------------------------------------------------------------ */
#define JUNO_CHORUS_MIN_MS    1.66f
#define JUNO_CHORUS_MAX_MS    5.35f
#define JUNO_CHORUS_I_HZ      0.513f
#define JUNO_CHORUS_II_HZ     0.863f
#define JUNO_CHORUS_III_HZ    9.75f    /* I+II, mono */
#define JUNO_CHORUS_BUF_MS    8.0f
#define JUNO_BBD_STAGES       256.0f
#define JUNO_BBD_INPUT_LP_HZ  7000.0f  /* the 12 dB low-pass ahead of the BBD */

/* ------------------------------------------------------------------------ */
/* Arpeggiator                                                               */
/*                                                                           */
/* Specifications page: ARPEGGIO RATE 1.5 .. 50 Hz. Panel Board A carries the */
/* on/off switch, a MODE switch (up, up and down, down) and a RANGE switch    */
/* (one, two or three octaves); the rear panel has an ARPEGGIO CLOCK input,   */
/* which this does not have a socket for.                                     */
/*                                                                           */
/* Not part of a patch -- the switches live in the CPU's switch matrix, not   */
/* in the patch memory, so changing sound mid-performance leaves the arpeggio */
/* running.                                                                   */
/* ------------------------------------------------------------------------ */
#define JUNO_ARP_HZ_MIN      1.5f
#define JUNO_ARP_HZ_MAX     50.0f
#define JUNO_ARP_MODES        3
#define JUNO_ARP_RANGES       3
#define JUNO_ARP_MAX_KEYS    16    /* held keys the pattern is built from */

/* Fraction of each step the note actually sounds for. The arpeggio clock puts
 * out a gate rather than a continuous level, and a Juno arpeggio is staccato:
 * at 100 % the contour would never retrigger and it would sound like one long
 * note sliding about. */
#define JUNO_ARP_GATE         0.55f

/* ------------------------------------------------------------------------ */
/* Output stage                                                              */
/* ------------------------------------------------------------------------ */
#define JUNO_DC_BLOCK_HZ     12.0f
#define JUNO_HISS_LEVEL       3.0e-5f

/* Pitch bender: the Juno-60 bender is a lever, and its depth is set by the
 * Bend Sens (DCO) control on the bender panel rather than being fixed. That
 * control's own maximum is seven semitones -- the master oscillator takes
 * +/-700 cents from the bender (service notes, master oscillator), and the
 * bender adjustment proves it twice over: with the lever hard left an E5
 * must read 442 Hz, and with it hard right a D4 must read 442 Hz, each a
 * fifth away from A4.
 *
 * This offered twelve before, which the instrument cannot reach. Two remains
 * the default, since that is what a MIDI controller expects and it is well
 * inside the range. */
#define JUNO_BEND_MAX_SEMITONES 7.0f

/* ------------------------------------------------------------------------ */
/* Tuning                                                                    */
/* ------------------------------------------------------------------------ */
/*
 * A deliberate departure, recorded here rather than left to look like an
 * oversight: a Juno-60 is tuned to A = 442 Hz, not 440.
 *
 * The service notes say so four times over -- the master oscillator
 * adjustment ("hold down A4 key and adjust L1 for 442 Hz"), both halves of
 * the bender adjustment, the key designation figure, and the frequency table
 * in the DCB section, which lists A4 at 442.0 Hz.
 *
 * 440 is kept because this instrument is played alongside others through
 * MIDI, where being two hertz sharp is a fault rather than a period detail.
 * Anyone who wants the original pitch has it on the panel: the rear Tune
 * control spans +/-50 cents and 442 is 7.85 cents up.
 */
#define JUNO_A4_HZ          440.0f
#define JUNO_NOTE0_HZ       8.1757989157f

#endif /* JUNO_DEFS_H */
