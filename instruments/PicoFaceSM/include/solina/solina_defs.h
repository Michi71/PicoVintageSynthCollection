/*
  PicoFaceSM -- ARP Solina String Ensemble

  Rebuilt from the original schematic (doc/ARP Solina Schematics.pdf, sheet
  015.0214 "Signal Flow Diagram" and sheet 015.0212 "Schematic Diagram"). The
  DSP models follow string-machine by Jean-Pierre Cimalando (Boost Software
  License 1.0), which in turn builds on a model by Peter Whiting.

  Signal flow of the original:

    Master Oscillator (SAA1004) + Tuning
      -> Divider Circuit: 9x SAJ110 dividers -> Sawtooth Circuits
      -> Gate Circuit: 10x TDA470 gates (4' and 8' per key)
         + Sustain Circuits
         -> Gate Output Circuit  --> VIOLA (8')    / VIOLIN (4')
         -> Formant Circuit TR5  --> TRUMPET (8')  / HORN (4')
      -> Bass Circuit: Low-Tone Selection -> Clipper -> Bass Sustain
         -> CELLO (8') / CONTRA BASS (16') -> Low-Pass
      -> Register Circuit -> VCA -> Low-Pass
      -> Modulator Circuit I/II/III (a TCA350Y BBD each)
      -> Output Amplifier -> Correction Filter -> Out

    Control Circuit: a tremolo oscillator (fast) and a chorus oscillator
    (slow), each routed through a low-pass, a phase shift and an inverter to
    the three modulator circuits C1/C2/C3.

  The Solina is not a polysynth but an organ with frequency dividers. Every
  note comes from one master oscillator and is phase-locked to the others;
  there is no detuning between voices. The registers are filter taps, not
  waveforms. All the movement in the sound comes from the ensemble.
*/

#ifndef SOLINA_DEFS_H
#define SOLINA_DEFS_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Host/target switch (same approach as PicoFaceCP / PicoFaceYC)             */
/* ------------------------------------------------------------------------ */
#ifdef SOLINA_HOST_BUILD
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

#define SOLINA_BLOCK        I2S_BUFFER_WORDS

/* ------------------------------------------------------------------------ */
/* Keyboard                                                                  */
/*                                                                           */
/* The original has 49 keys. The schematic (Panel A) splits them into five   */
/* groups, each of which sees its own RC network in the Gate Output Circuit  */
/* -- a keyboard split of the timbre. That structure is kept here: filters   */
/* sit per group behind the summing bus, not per note. This is both faithful */
/* to the circuit and orders of magnitude cheaper than one filter set per   */
/* voice.                                                                   */
/* ------------------------------------------------------------------------ */
#define SOLINA_KEY_FIRST    36      /* C2 */
#define SOLINA_KEY_LAST     84      /* C6 -- 49 keys */
#define SOLINA_NGROUPS      5
#define SOLINA_KEYS_PER_GROUP 10

/* Bass section: the Low-Tone Selection Circuit covers the lower two octaves
 * and works with lowest-note priority (a single note). */
#define SOLINA_BASS_LAST    59      /* B3 */

/* Maximum number of keys sounding at once. Divide-down has no voice limit as
 * such; this is only the size of the active list. */
#ifndef SOLINA_MAX_ACTIVE_KEYS
#define SOLINA_MAX_ACTIVE_KEYS 49
#endif

/* ------------------------------------------------------------------------ */
/* Ensemble (Modulator Circuit I/II/III)                                     */
/*                                                                           */
/* Three TCA350Y BBD lines. The delay is around 5 ms and is modulated by     */
/* +/- 1 ms (calibration taken from string-machine, Delay3Phase.cpp).        */
/*                                                                           */
/* The original is mono and sums the three lines with a sign matrix. Here    */
/* the outputs are combined as mid/side instead, which keeps the mono sum    */
/* identical while giving the stereo image a width control:                  */
/*     mid = (d1 + d2 + d3) * 2/3      side = (d1 - d3) * width              */
/* ------------------------------------------------------------------------ */
#define SOLINA_ENSEMBLE_LINES     3
#define SOLINA_ENSEMBLE_DELAY_MS  5.0f
#define SOLINA_ENSEMBLE_VAR_MS    1.0f
#define SOLINA_ENSEMBLE_MAX_MS    8.0f   /* buffer size per line */

/* Control Circuit: frequency ranges of the two control oscillators, derived
 * from the component values in the schematic:
 *   Tremolo: 2M2 + 1M trimmer, 68n   -> a few Hz
 *   Chorus : 1M8 + 1M trimmer, 680n  -> below 1 Hz
 * The limits agree with string-machine (LFO3PhaseDual.dsp). */
#define SOLINA_TREMOLO_HZ_MIN   3.0f
#define SOLINA_TREMOLO_HZ_MAX   9.0f
#define SOLINA_CHORUS_HZ_MIN    0.3f
#define SOLINA_CHORUS_HZ_MAX    0.9f

/* Anti-aliasing chain ahead of the delay lines
 * (string-machine, Delay3PhaseDigital.dsp) */
#define SOLINA_AA_F1  9561.0f   /* midikey2hz(122.3) */
#define SOLINA_AA_Q1  1.4706f   /* 1/(2-2*0.66)      */
#define SOLINA_AA_F2  9561.0f
#define SOLINA_AA_Q2  1.4706f
#define SOLINA_AA_F3  5751.0f   /* midikey2hz(113.5) */
#define SOLINA_AA_Q3  1.0870f   /* 1/(2-2*0.54)      */

/* Reconstruction filter behind the delay lines.
 *
 * Modulator Circuit I (doc/StringEnsemble_Schematics-0275.pdf, page 4) has
 * TWO cascaded active low-passes behind the BBD ORB 33:
 *   stage 1  TR5 BC169B, 22K(36)/22K(52)/1K(51), 8n2(37) and 47p(44)
 *   stage 2  TR4 BC169B, 22K(48)/1K(45),         2n7(46) and 560p(47)
 * only then come the 2K2(39) level trimmer and the summing stage.
 *
 * The corner frequencies are estimated from the component values (Sallen-Key,
 * f = 1/(2*pi*R*sqrt(C1*C2)) with R = 22K) -- not from a worked-out transfer
 * function. The transistor stages cannot be traced completely in the scan.
 * Adjustable at runtime through the "Ens Tone" parameter. */
#define SOLINA_RECON_F1 11653.0f
#define SOLINA_RECON_F2  5883.0f
#define SOLINA_RECON_Q   0.7071f

/* ------------------------------------------------------------------------ */
/* Phaser -- an addition of the Behringer remake, not in the original       */
/*                                                                          */
/* Behringer manual, "Modulation Section": buttons Modulation, phaser /     */
/* controls Color, rate. Six all-pass stages, sweeping 200 Hz .. 1600 Hz.    */
/* ------------------------------------------------------------------------ */
#define SOLINA_PHASER_HZ_MIN   0.05f
#define SOLINA_PHASER_HZ_MAX   8.0f
#define SOLINA_PHASER_F_MIN  200.0f
#define SOLINA_PHASER_F_MAX 1600.0f

/* ------------------------------------------------------------------------ */
/* Output stage                                                              */
/*                                                                           */
/* Threshold of the soft limiter. Below -3.1 dBFS the curve is exactly       */
/* linear; in normal playing the factory programs stay far below that and    */
/* are not touched at all. Only dense clusters run into the limiter, instead */
/* of being hard-clipped at the int16 boundary.                              */
/* ------------------------------------------------------------------------ */
#define SOLINA_CLIP_THRESHOLD 0.70f

/* ------------------------------------------------------------------------ */
/* Tuning                                                                    */
/* ------------------------------------------------------------------------ */
#define SOLINA_A4_HZ        440.0f
/* Frequency of MIDI note 0 (C-1) */
#define SOLINA_NOTE0_HZ     8.1757989157f

#endif /* SOLINA_DEFS_H */
