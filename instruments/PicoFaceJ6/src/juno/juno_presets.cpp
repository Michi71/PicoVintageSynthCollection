// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_presets.cpp -- factory patch settings

  All 56 factory patches, transcribed from the chart on pages 25 to 28 of the
  Juno-60 owner's manual. That chart prints the complete front panel of every
  patch on the instrument's own 0..10 scale, and it settles what was previously
  imported from junox and believed to be unpublished.

  The transcription was checked against the imported set cell by cell. Of 720
  numeric cells across the 48 patches the two sets share, 590 agree exactly and
  the remaining 130 fall into four groups:

    125  junox truncated the half-steps. Wherever the chart says 6.5 it had 6.
         That is the bulk of what changes here.
      3  junox misread a digit: patch 33 pulse width 8 not 9, and both
         Pizzicato patches resonance 3 not 4. Re-read at high magnification.
      2  deliberate, and kept -- see below.

  Two further columns junox dropped altogether:

    The octave transpose. Every imported patch sat at 8'; the chart puts
    roughly twenty of them at 16' or 4', and it reads like an instrument list
    when it does -- both basses and both clavichords down, xylophone,
    glockenspiel, flute and the reeds up, tuba down.

    Bank 7, eight patches. The owner's manual explains why nobody reproduces
    them: "Bank 7 includes the patches whose sound sources are VCF self-
    oscillation." Their chart rows carry no waveform at all and resonance 10
    throughout, so they sound only if the filter really sings.

  Not transcribed: the VCA level column. It is printed as a signed value in a
  range no 0..10 slider has, and it defeated junox (which substituted a
  constant 7) and the Patch Book author (who drew every level slider at the
  same height) as well as this reading. Each patch therefore keeps the level it
  shipped with, and bank 7 takes the same 0.700 the rest mostly use.

  Two values depart from the chart, both kept from a hardware listening test:

    Piano 1       DCO LFO 4.5 -> 0
    Clavichord 1  DCO LFO 4.0 -> 0
        The chart really does put vibrato on both. Neither instrument has any,
        and with the trigger mode on manual the LFO runs free rather than
        waiting for the button, so it is heard on every note. At the corrected
        modulation depth of three semitones this would be over a hundred cents
        on a piano. Worth a listen before it is trusted either way.

    Brass         VCA level 0.7 -> 1.0
        Also from that test, and independently supported: the chart's own level
        column ranks Brass above the strings, whatever its scale turns out to
        be.

  Each entry is a complete front panel, in the order of the enum in
  juno_params.h and grouped by the sections of the instrument. The macros make
  the grouping visible and, more usefully, make a miscounted row a compile
  error rather than a patch with a silently zeroed tail: LFO takes two
  arguments, DCO nine, HPF_ one, VCF six, VCA two, ENV four, CHOR one and SYS
  four, and together they expand to exactly JUNO_PARAM_COUNT values.

  Positions of rotary switches are written as R3(1) and so on -- the middle of
  the slot, so a value surviving a round trip through the per mille IPC or the
  settings record cannot land one position over.
*/

#include "juno/juno_presets.h"

/* Middle of slot i of an n-position switch. */
#define R2(i)  (((float) (i) + 0.5f) /  2.0f)
#define R3(i)  (((float) (i) + 0.5f) /  3.0f)
#define R4(i)  (((float) (i) + 0.5f) /  4.0f)
#define R5(i)  (((float) (i) + 0.5f) /  5.0f)
#define R8(i)  (((float) (i) + 0.5f) /  8.0f)

#define OFF 0.0f
#define ON  1.0f

/* Range switch. Every patch in the source set is at 8'. */
#define F16 R3(0)
#define F8  R3(1)
#define F4  R3(2)

/* Pulse width mode selector */
#define PWM_LFO R3(0)
#define PWM_MAN R3(1)
#define PWM_ENV R3(2)

#define VCA_ENV  R2(0)
#define VCA_GATE R2(1)

#define POL_NEG R2(0)
#define POL_POS R2(1)

#define CH_OFF R4(0)
#define CH_I   R4(1)
#define CH_II  R4(2)
#define CH_III R4(3)

#define TRIG_AUTO R2(0)
#define TRIG_MAN  R2(1)

#define OCT0  R5(2)
#define BEND2 R8(2)

#define LFO(rate, delay)  rate, delay

#define DCO(range, lfo, pwm, pwmMode, saw, pulse, sub, subLevel, noise) \
    range, lfo, pwm, pwmMode, saw, pulse, sub, subLevel, noise

#define HPF_(pos) pos

#define VCF(freq, res, env, pol, lfo, kybd) freq, res, env, pol, lfo, kybd

#define VCA(level, mode) level, mode

#define ENV(a, d, s, r) a, d, s, r

#define CHOR(mode) mode

/* Tune always at the detent: a patch that arrives out of tune is a fault. */
#define SYS(trig, oct) 0.5f, BEND2, trig, oct

const JunoProgram junoPrograms[JUNO_NPROGRAMS] = {

{ "Strings 1", {
    LFO(0.600f, 0.000f),
    DCO(F8 , 0.000f, 0.000f, PWM_LFO,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.700f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.000f, 1.000f, 0.450f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Strings 2", {
    LFO(0.400f, 0.000f),
    DCO(F8 , 0.000f, 0.600f, PWM_LFO,
        ON , ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.700f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.000f, 1.000f, 0.450f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Strings 3", {
    LFO(0.300f, 0.800f),
    DCO(F8 , 0.000f, 0.700f, PWM_LFO,
        ON , ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.300f, 0.000f, 1.000f, 0.600f),
    CHOR(CH_II),
    SYS(TRIG_MAN , OCT0) }},

{ "Organ 1", {
    LFO(0.200f, 0.800f),
    DCO(F8 , 0.000f, 0.500f, PWM_MAN,
        OFF, ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.400f, 0.600f, 0.450f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.000f, 0.000f, 0.000f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Organ 2", {
    LFO(0.500f, 0.400f),
    DCO(F8 , 0.000f, 0.550f, PWM_LFO,
        OFF, ON , ON , 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.350f, 0.550f, 0.400f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.100f, 0.000f, 0.100f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Organ 3", {
    LFO(0.500f, 0.400f),
    DCO(F4 , 0.000f, 0.550f, PWM_LFO,
        OFF, ON , ON , 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.350f, 0.550f, 0.350f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.100f, 0.000f, 0.100f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Brass", {
    LFO(0.500f, 0.650f),
    DCO(F8 , 0.150f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.000f, 0.000f, 0.850f, POL_POS, 0.000f, 0.400f),
    VCA(1.000f, VCA_ENV),
    ENV(0.250f, 0.400f, 0.600f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Phase Brass", {
    LFO(0.600f, 0.000f),
    DCO(F8 , 0.000f, 1.000f, PWM_ENV,
        ON , ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.100f, 0.550f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.200f, 0.400f, 0.400f, 0.300f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Piano 1", {
    LFO(0.600f, 0.300f),
    DCO(F8 , 0.000f, 0.600f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.100f, 0.000f, 0.700f, POL_POS, 0.000f, 0.400f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.800f, 0.150f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN , OCT0) }},

{ "Piano 2", {
    LFO(0.400f, 0.000f),
    DCO(F4 , 0.000f, 0.400f, PWM_MAN,
        OFF, ON , ON , 0.450f, 0.000f),
    HPF_(R4(0)),
    VCF(0.350f, 0.000f, 0.250f, POL_POS, 0.200f, 0.800f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.750f, 0.000f, 0.350f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Celesta", {
    LFO(0.350f, 0.600f),
    DCO(F8 , 0.000f, 0.500f, PWM_ENV,
        ON , ON , OFF, 1.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.350f, 0.800f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.650f, 0.200f, 0.550f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Mellow Piano", {
    LFO(0.500f, 0.000f),
    DCO(F8 , 0.000f, 0.500f, PWM_MAN,
        OFF, ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.250f, POL_POS, 0.100f, 0.900f),
    VCA(0.700f, VCA_ENV),
    ENV(0.100f, 0.750f, 0.200f, 0.850f),
    CHOR(CH_I),
    SYS(TRIG_MAN , OCT0) }},

{ "Harpsichord 1", {
    LFO(0.500f, 0.400f),
    DCO(F4 , 0.000f, 0.300f, PWM_MAN,
        OFF, ON , ON , 0.700f, 0.000f),
    HPF_(R4(1)),
    VCF(0.300f, 0.000f, 0.500f, POL_POS, 0.000f, 0.700f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.600f, 0.350f, 0.250f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Harpsichord 2", {
    LFO(0.550f, 0.600f),
    DCO(F4 , 0.000f, 0.200f, PWM_MAN,
        OFF, ON , ON , 0.850f, 0.000f),
    HPF_(R4(1)),
    VCF(0.500f, 0.250f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.150f, 0.500f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Guitar", {
    LFO(0.600f, 0.600f),
    DCO(F8 , 0.000f, 0.600f, PWM_MAN,
        OFF, ON , OFF, 1.000f, 0.000f),
    HPF_(R4(2)),
    VCF(0.300f, 0.000f, 0.450f, POL_POS, 0.150f, 0.500f),
    VCA(0.750f, VCA_ENV),
    ENV(0.000f, 0.550f, 0.350f, 0.650f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Synth Harp", {
    LFO(0.300f, 0.800f),
    DCO(F8 , 0.000f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.500f, POL_POS, 0.000f, 0.800f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.550f, 0.300f, 0.500f),
    CHOR(CH_I),
    SYS(TRIG_MAN , OCT0) }},

{ "Bass 1", {
    LFO(0.500f, 0.600f),
    DCO(F16, 0.000f, 0.500f, PWM_MAN,
        ON , ON , ON , 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.250f, 0.350f, POL_POS, 0.000f, 0.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.400f, 0.100f, 0.250f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Bass 2", {
    LFO(0.500f, 0.600f),
    DCO(F16, 0.000f, 0.500f, PWM_MAN,
        ON , ON , OFF, 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.500f, 0.450f, POL_POS, 0.000f, 0.500f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.300f, 0.350f, 0.250f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Clavichord 1", {
    LFO(0.600f, 0.250f),
    DCO(F16, 0.000f, 0.800f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.000f, 0.300f, 0.800f, POL_POS, 0.000f, 0.600f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.350f, 0.150f),
    CHOR(CH_I),
    SYS(TRIG_MAN , OCT0) }},

{ "Clavichord 2", {
    LFO(0.100f, 0.000f),
    DCO(F16, 0.000f, 0.800f, PWM_MAN,
        OFF, ON , OFF, 1.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.550f, 0.700f, 0.200f, POL_POS, 0.250f, 0.700f),
    VCA(0.850f, VCA_ENV),
    ENV(0.000f, 0.450f, 0.200f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Pizzicato Snd1", {
    LFO(0.500f, 0.600f),
    DCO(F8 , 0.000f, 0.350f, PWM_MAN,
        OFF, ON , OFF, 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.450f, 0.300f, 0.300f, POL_POS, 0.300f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.200f, 0.350f, 0.550f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Pizzicato Snd2", {
    LFO(0.500f, 0.600f),
    DCO(F4 , 0.000f, 0.200f, PWM_MAN,
        OFF, ON , ON , 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.300f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.300f, 0.400f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Xylophone", {
    LFO(0.500f, 0.000f),
    DCO(F4 , 0.000f, 0.500f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.400f, 0.500f, 0.300f, POL_POS, 0.000f, 0.600f),
    VCA(0.850f, VCA_ENV),
    ENV(0.000f, 0.350f, 0.000f, 0.350f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Glockenspiel", {
    LFO(0.500f, 0.000f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.450f, 0.500f, 0.300f, POL_POS, 0.000f, 0.600f),
    VCA(0.750f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.250f, 0.500f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Violine", {
    LFO(0.600f, 0.600f),
    DCO(F8 , 0.200f, 0.000f, PWM_LFO,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.650f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.000f, 1.000f, 0.400f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Trumpet", {
    LFO(0.250f, 0.650f),
    DCO(F8 , 0.150f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.000f, 0.000f, 0.850f, POL_POS, 0.000f, 0.400f),
    VCA(0.700f, VCA_ENV),
    ENV(0.250f, 0.400f, 0.600f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Horn", {
    LFO(0.250f, 0.700f),
    DCO(F8 , 0.000f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.000f, 0.550f, POL_POS, 0.200f, 0.400f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.500f, 0.600f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Tuba", {
    LFO(0.250f, 0.700f),
    DCO(F16, 0.150f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.150f, 0.000f, 0.600f, POL_POS, 0.000f, 0.400f),
    VCA(0.850f, VCA_ENV),
    ENV(0.300f, 0.400f, 0.400f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Flute", {
    LFO(0.550f, 0.500f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.150f),
    HPF_(R4(1)),
    VCF(0.500f, 0.000f, 0.000f, POL_POS, 0.200f, 0.600f),
    VCA(0.850f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.500f, 0.250f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Clarinet", {
    LFO(0.500f, 0.650f),
    DCO(F8 , 0.150f, 0.000f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.500f, 0.300f, 0.250f, POL_POS, 0.000f, 0.600f),
    VCA(0.700f, VCA_ENV),
    ENV(0.250f, 0.600f, 0.600f, 0.250f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Oboe", {
    LFO(0.550f, 0.650f),
    DCO(F8 , 0.150f, 0.650f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(3)),
    VCF(0.450f, 0.500f, 0.250f, POL_POS, 0.000f, 0.500f),
    VCA(0.850f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.600f, 0.250f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "English Horn", {
    LFO(0.500f, 0.700f),
    DCO(F16, 0.200f, 0.650f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(3)),
    VCF(0.500f, 0.700f, 0.000f, POL_POS, 0.150f, 0.500f),
    VCA(0.850f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.600f, 0.250f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Funny Cat", {
    LFO(0.600f, 0.200f),
    DCO(F8 , 0.300f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.150f, 0.750f, 0.500f, POL_POS, 0.200f, 0.500f),
    VCA(0.700f, VCA_ENV),
    ENV(0.250f, 0.400f, 1.000f, 0.100f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN , OCT0) }},

{ "Wah Brass", {
    LFO(0.600f, 0.200f),
    DCO(F8 , 0.300f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.700f, 0.450f, POL_POS, 0.000f, 0.600f),
    VCA(0.700f, VCA_GATE),
    ENV(0.300f, 0.300f, 0.400f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN , OCT0) }},

{ "Phase Combin.", {
    LFO(0.600f, 0.200f),
    DCO(F8 , 0.000f, 0.800f, PWM_MAN,
        ON , ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.200f, 0.300f, POL_POS, 0.000f, 0.200f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.700f, 0.200f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_MAN , OCT0) }},

{ "Reed 1", {
    LFO(0.600f, 0.200f),
    DCO(F8 , 0.400f, 0.000f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.100f, 0.600f, 0.700f, POL_POS, 0.000f, 0.500f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.850f, 0.500f, 0.100f),
    CHOR(CH_I),
    SYS(TRIG_MAN , OCT0) }},

{ "Popcorn", {
    LFO(0.000f, 0.000f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.250f, 0.200f, 0.550f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.200f, 0.000f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Reed 2", {
    LFO(0.300f, 0.800f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.000f, 0.600f, POL_POS, 0.000f, 0.800f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.550f, 0.300f, 0.600f),
    CHOR(CH_I),
    SYS(TRIG_MAN , OCT0) }},

{ "Reed 3", {
    LFO(0.600f, 0.200f),
    DCO(F4 , 0.200f, 0.500f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.200f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.250f, 0.000f, 1.000f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN , OCT0) }},

{ "PWM Chorus", {
    LFO(0.300f, 0.000f),
    DCO(F8 , 0.000f, 0.500f, PWM_LFO,
        OFF, ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.800f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.300f, 0.000f, 1.000f, 0.400f),
    CHOR(CH_II),
    SYS(TRIG_MAN , OCT0) }},

{ "Synth Organ", {
    LFO(0.450f, 0.600f),
    DCO(F8 , 0.000f, 0.650f, PWM_MAN,
        OFF, ON , ON , 0.750f, 0.000f),
    HPF_(R4(0)),
    VCF(0.250f, 0.000f, 0.500f, POL_POS, 0.200f, 0.700f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.200f, 0.500f, 0.250f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Effect Sound 1", {
    LFO(0.450f, 0.600f),
    DCO(F4 , 0.150f, 1.000f, PWM_MAN,
        ON , ON , ON , 0.700f, 0.000f),
    HPF_(R4(0)),
    VCF(0.650f, 0.000f, 0.450f, POL_NEG, 0.000f, 0.700f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.000f, 0.550f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Effect Sound 2", {
    LFO(0.550f, 0.900f),
    DCO(F8 , 0.000f, 0.300f, PWM_LFO,
        ON , OFF, ON , 0.650f, 0.000f),
    HPF_(R4(0)),
    VCF(0.650f, 0.300f, 0.400f, POL_NEG, 0.000f, 0.100f),
    VCA(0.700f, VCA_GATE),
    ENV(0.650f, 0.550f, 0.200f, 0.650f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Harp", {
    LFO(0.550f, 0.000f),
    DCO(F8 , 0.200f, 0.000f, PWM_ENV,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.650f, 0.500f, 0.550f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.800f, 0.800f, 0.900f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Funk", {
    LFO(0.300f, 0.250f),
    DCO(F8 , 0.000f, 0.600f, PWM_MAN,
        ON , ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.750f, 0.600f, 0.500f, POL_NEG, 0.000f, 0.450f),
    VCA(0.700f, VCA_GATE),
    ENV(0.600f, 0.500f, 0.000f, 0.000f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Sound 1", {
    LFO(0.600f, 0.700f),
    DCO(F8 , 0.200f, 0.450f, PWM_MAN,
        OFF, ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.650f, 0.700f, 0.550f, POL_NEG, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.800f, 0.000f, 0.300f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Mysterious Inv", {
    LFO(0.600f, 0.800f),
    DCO(F8 , 0.200f, 0.800f, PWM_ENV,
        ON , ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.800f, 0.700f, 0.600f, POL_NEG, 0.250f, 0.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 1.000f, 0.000f, 1.000f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Sound 2", {
    LFO(0.300f, 0.300f),
    DCO(F8 , 0.000f, 0.600f, PWM_MAN,
        ON , ON , OFF, 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.850f, 0.600f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(1.000f, 1.000f, 1.000f, 1.000f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Percussive Sd1", {
    LFO(0.000f, 0.000f),
    DCO(F4 , 0.000f, 0.000f, PWM_ENV,
        OFF, OFF, OFF, 0.000f, 1.000f),
    HPF_(R4(1)),
    VCF(0.400f, 1.000f, 0.150f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.000f, 0.400f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Percussive Sd2", {
    LFO(0.000f, 0.000f),
    DCO(F8 , 0.000f, 0.000f, PWM_ENV,
        OFF, OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.500f, 1.000f, 0.350f, POL_NEG, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.000f, 0.400f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Whistle", {
    LFO(0.550f, 0.500f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, OFF, 0.000f, 0.200f),
    HPF_(R4(1)),
    VCF(0.350f, 1.000f, 0.150f, POL_POS, 0.200f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.300f, 0.000f, 1.000f, 0.100f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Effect Sound 3", {
    LFO(0.550f, 0.400f),
    DCO(F8 , 0.000f, 0.000f, PWM_ENV,
        OFF, OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.350f, 1.000f, 0.000f, POL_POS, 0.200f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.400f, 0.550f, 0.700f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "UFO", {
    LFO(0.600f, 0.000f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, OFF, 0.000f, 0.200f),
    HPF_(R4(0)),
    VCF(0.000f, 1.000f, 0.700f, POL_POS, 0.400f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.600f, 1.000f, 0.800f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Sound 3", {
    LFO(0.600f, 0.000f),
    DCO(F4 , 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, OFF, 0.000f, 0.200f),
    HPF_(R4(0)),
    VCF(0.500f, 1.000f, 0.400f, POL_NEG, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 1.000f, 0.000f, 0.800f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Surf", {
    LFO(0.000f, 0.000f),
    DCO(F8 , 0.000f, 0.000f, PWM_ENV,
        OFF, OFF, OFF, 1.000f, 1.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.000f, 0.000f, POL_POS, 0.600f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.400f, 1.000f, 0.800f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Synth Drum", {
    LFO(0.000f, 0.000f),
    DCO(F8 , 0.000f, 0.000f, PWM_ENV,
        OFF, OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 1.000f, 0.400f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.000f, 0.600f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN , OCT0) }},

};

static_assert(sizeof(junoPrograms) / sizeof(junoPrograms[0]) == JUNO_NPROGRAMS,
              "patch table and JUNO_NPROGRAMS have drifted apart");
