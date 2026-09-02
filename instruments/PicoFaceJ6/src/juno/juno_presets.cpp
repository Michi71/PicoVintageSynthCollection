// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_presets.cpp -- factory patch settings

  The Juno-60 stores 56 patches of its own (8 per bank, 7 banks), and the
  factory set is not published in the service notes. These 48 come from the
  patch table of junox (GPL v3, the same licence as this project), which
  reproduces the same parameters and is where their names come from.

  Each entry is a complete front panel, in the order of the enum in
  juno_params.h and grouped by the sections of the instrument. The macros make
  the grouping visible and, more usefully, make a miscounted row a compile
  error rather than a patch with a silently zeroed tail: LFO takes two
  arguments, DCO nine, HPF_ one, VCF six, VCA two, ENV four, CHOR one and SYS
  four, and together they expand to exactly JUNO_PARAM_COUNT values.

  Positions of rotary switches are written as R3(1) and so on -- the middle of
  the slot, so a value surviving a round trip through the per mille IPC or the
  settings record cannot land one position over.

  Three values depart from the imported set, each because the sound and the
  name disagreed:

    Piano I       DCO LFO 0.4 -> 0
    Clavichord I  DCO LFO 0.4 -> 0
        Forty cents of vibrato at five hertz. Neither instrument has any
        vibrato at all, and it is what a listener described as a ghost. The
        other fourteen patches that use the DCO LFO keep it -- ten to twenty
        cents on a violin, a clarinet or an oboe is what those instruments do.

    Brass         VCA level 0.7 -> 1.0
        The timbre was right and the level sat six decibels under everything
        else, which for a brass patch is the wrong way round.
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

{ "Strings I", {
    LFO(0.600f, 0.000f),
    DCO(F8, 0.000f, 0.000f, PWM_LFO,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.700f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.000f, 1.000f, 0.400f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Strings II", {
    LFO(0.400f, 0.000f),
    DCO(F8, 0.000f, 0.600f, PWM_LFO,
        ON , ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.700f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.000f, 1.000f, 0.400f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Strings III", {
    LFO(0.300f, 0.800f),
    DCO(F8, 0.000f, 0.700f, PWM_LFO,
        ON , ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.300f, 0.000f, 1.000f, 0.600f),
    CHOR(CH_II),
    SYS(TRIG_MAN, OCT0) }},

{ "Organ I", {
    LFO(0.200f, 0.800f),
    DCO(F8, 0.000f, 0.500f, PWM_MAN,
        OFF, ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.400f, 0.600f, 0.400f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.000f, 0.000f, 0.000f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Organ II", {
    LFO(0.500f, 0.400f),
    DCO(F8, 0.000f, 0.500f, PWM_LFO,
        OFF, ON , ON , 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.500f, 0.400f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.100f, 0.000f, 0.100f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Organ III", {
    LFO(0.500f, 0.400f),
    DCO(F8, 0.000f, 0.500f, PWM_LFO,
        OFF, ON , ON , 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.500f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.100f, 0.000f, 0.100f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Brass", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.100f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.000f, 0.000f, 0.800f, POL_POS, 0.000f, 0.400f),
    VCA(1.000f, VCA_ENV),
    ENV(0.200f, 0.400f, 0.600f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Phase Brass", {
    LFO(0.600f, 0.000f),
    DCO(F8, 0.000f, 1.000f, PWM_ENV,
        ON , ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.100f, 0.500f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.200f, 0.400f, 0.400f, 0.300f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Piano I", {
    LFO(0.600f, 0.300f),
    DCO(F8, 0.000f, 0.600f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.100f, 0.000f, 0.700f, POL_POS, 0.000f, 0.400f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.800f, 0.100f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN, OCT0) }},

{ "Piano II", {
    LFO(0.400f, 0.000f),
    DCO(F8, 0.000f, 0.400f, PWM_MAN,
        OFF, ON , ON , 0.400f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.200f, POL_POS, 0.200f, 0.800f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.700f, 0.000f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Celesta", {
    LFO(0.300f, 0.600f),
    DCO(F8, 0.000f, 0.500f, PWM_ENV,
        ON , ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.800f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.600f, 0.200f, 0.500f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Mellow Piano", {
    LFO(0.500f, 0.000f),
    DCO(F8, 0.000f, 0.500f, PWM_MAN,
        OFF, ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.200f, POL_POS, 0.100f, 0.900f),
    VCA(0.700f, VCA_ENV),
    ENV(0.100f, 0.700f, 0.200f, 0.800f),
    CHOR(CH_I),
    SYS(TRIG_MAN, OCT0) }},

{ "Harpsichord I", {
    LFO(0.500f, 0.400f),
    DCO(F8, 0.000f, 0.300f, PWM_MAN,
        OFF, ON , ON , 0.700f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.500f, POL_POS, 0.000f, 0.700f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.600f, 0.300f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Harpsicord II", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.000f, 0.200f, PWM_MAN,
        OFF, ON , ON , 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.200f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.100f, 0.500f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Guitar", {
    LFO(0.600f, 0.600f),
    DCO(F8, 0.000f, 0.600f, PWM_MAN,
        OFF, ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.400f, POL_POS, 0.100f, 0.500f),
    VCA(0.750f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.300f, 0.600f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Synthetiser Har", {
    LFO(0.300f, 0.800f),
    DCO(F8, 0.000f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.000f, 0.500f, POL_POS, 0.000f, 0.800f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.300f, 0.500f),
    CHOR(CH_I),
    SYS(TRIG_MAN, OCT0) }},

{ "Bass I", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.000f, 0.500f, PWM_MAN,
        ON , ON , ON , 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.200f, 0.300f, POL_POS, 0.000f, 0.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.400f, 0.100f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Bass II", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.000f, 0.500f, PWM_MAN,
        ON , ON , OFF, 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.500f, 0.400f, POL_POS, 0.000f, 0.500f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.300f, 0.300f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Clavichord I", {
    LFO(0.600f, 0.200f),
    DCO(F8, 0.000f, 0.900f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.000f, 0.300f, 0.800f, POL_POS, 0.000f, 0.600f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.300f, 0.100f),
    CHOR(CH_I),
    SYS(TRIG_MAN, OCT0) }},

{ "Clavichord II", {
    LFO(0.100f, 0.000f),
    DCO(F8, 0.000f, 0.800f, PWM_MAN,
        OFF, ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.700f, 0.200f, POL_POS, 0.200f, 0.700f),
    VCA(0.850f, VCA_ENV),
    ENV(0.000f, 0.400f, 0.200f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Pizzicato Sound", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.000f, 0.300f, PWM_MAN,
        OFF, ON , OFF, 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.400f, 0.400f, 0.300f, POL_POS, 0.300f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.200f, 0.300f, 0.500f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Pizzicato Sound", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.000f, 0.200f, PWM_MAN,
        OFF, ON , ON , 0.300f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.400f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.300f, 0.400f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Xylophone", {
    LFO(0.500f, 0.000f),
    DCO(F8, 0.000f, 0.500f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.400f, 0.500f, 0.300f, POL_POS, 0.000f, 0.600f),
    VCA(0.850f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.000f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Glockenspiel", {
    LFO(0.500f, 0.000f),
    DCO(F8, 0.000f, 0.000f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.400f, 0.500f, 0.300f, POL_POS, 0.000f, 0.600f),
    VCA(0.750f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.200f, 0.500f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Violin", {
    LFO(0.600f, 0.600f),
    DCO(F8, 0.200f, 0.000f, PWM_LFO,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.000f, 1.000f, 0.400f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Trumpet", {
    LFO(0.200f, 0.600f),
    DCO(F8, 0.100f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.000f, 0.000f, 0.800f, POL_POS, 0.000f, 0.400f),
    VCA(0.700f, VCA_ENV),
    ENV(0.200f, 0.400f, 0.600f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Horn", {
    LFO(0.200f, 0.700f),
    DCO(F8, 0.000f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.000f, 0.500f, POL_POS, 0.200f, 0.400f),
    VCA(0.700f, VCA_ENV),
    ENV(0.400f, 0.500f, 0.600f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Tube", {
    LFO(0.200f, 0.700f),
    DCO(F8, 0.100f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.100f, 0.000f, 0.600f, POL_POS, 0.000f, 0.400f),
    VCA(0.850f, VCA_ENV),
    ENV(0.300f, 0.400f, 0.400f, 0.300f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Flute", {
    LFO(0.500f, 0.500f),
    DCO(F8, 0.000f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.100f),
    HPF_(R4(0)),
    VCF(0.500f, 0.000f, 0.000f, POL_POS, 0.200f, 0.600f),
    VCA(0.850f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.500f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Clarinet", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.100f, 0.000f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.500f, 0.300f, 0.200f, POL_POS, 0.000f, 0.600f),
    VCA(0.700f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.600f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Oboe", {
    LFO(0.500f, 0.600f),
    DCO(F8, 0.100f, 0.600f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.400f, 0.500f, 0.200f, POL_POS, 0.000f, 0.500f),
    VCA(0.850f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.600f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "English Horn", {
    LFO(0.500f, 0.700f),
    DCO(F8, 0.200f, 0.600f, PWM_MAN,
        OFF, ON , OFF, 0.000f, 0.000f),
    HPF_(R4(1)),
    VCF(0.500f, 0.700f, 0.000f, POL_POS, 0.100f, 0.500f),
    VCA(0.850f, VCA_ENV),
    ENV(0.200f, 0.600f, 0.600f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Funny Cat", {
    LFO(0.600f, 0.200f),
    DCO(F8, 0.300f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.100f, 0.700f, 0.500f, POL_POS, 0.200f, 0.500f),
    VCA(0.700f, VCA_ENV),
    ENV(0.200f, 0.400f, 1.000f, 0.100f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN, OCT0) }},

{ "Wah Brass", {
    LFO(0.600f, 0.200f),
    DCO(F8, 0.300f, 0.000f, PWM_MAN,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.700f, 0.400f, POL_POS, 0.000f, 0.600f),
    VCA(0.700f, VCA_GATE),
    ENV(0.300f, 0.300f, 0.400f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN, OCT0) }},

{ "Phase Combinati", {
    LFO(0.600f, 0.200f),
    DCO(F8, 0.000f, 0.800f, PWM_MAN,
        ON , ON , OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.200f, 0.300f, POL_POS, 0.000f, 0.200f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.700f, 0.200f, 0.200f),
    CHOR(CH_I),
    SYS(TRIG_MAN, OCT0) }},

{ "Reed I", {
    LFO(0.600f, 0.200f),
    DCO(F8, 0.400f, 0.000f, PWM_MAN,
        OFF, ON , ON , 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.100f, 0.600f, 0.700f, POL_POS, 0.000f, 0.500f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.800f, 0.500f, 0.100f),
    CHOR(CH_I),
    SYS(TRIG_MAN, OCT0) }},

{ "Popcorn", {
    LFO(0.000f, 0.000f),
    DCO(F8, 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.200f, 0.500f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.300f, 0.200f, 0.000f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Reed II", {
    LFO(0.300f, 0.800f),
    DCO(F8, 0.000f, 0.000f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.000f, 0.600f, POL_POS, 0.000f, 0.800f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.300f, 0.600f),
    CHOR(CH_I),
    SYS(TRIG_MAN, OCT0) }},

{ "Reed III", {
    LFO(0.600f, 0.200f),
    DCO(F8, 0.200f, 0.500f, PWM_MAN,
        OFF, OFF, ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.300f, 0.200f, 0.300f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.200f, 0.000f, 1.000f, 0.200f),
    CHOR(CH_OFF),
    SYS(TRIG_MAN, OCT0) }},

{ "PWM Chorus", {
    LFO(0.300f, 0.000f),
    DCO(F8, 0.000f, 0.500f, PWM_LFO,
        OFF, ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.800f, 0.000f, 0.000f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.300f, 0.000f, 1.000f, 0.400f),
    CHOR(CH_II),
    SYS(TRIG_MAN, OCT0) }},

{ "Synthetiser Org", {
    LFO(0.400f, 0.600f),
    DCO(F8, 0.000f, 0.600f, PWM_MAN,
        OFF, ON , ON , 0.700f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.000f, 0.500f, POL_POS, 0.200f, 0.700f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.200f, 0.500f, 0.200f),
    CHOR(CH_II),
    SYS(TRIG_AUTO, OCT0) }},

{ "Effect Sound", {
    LFO(0.400f, 0.600f),
    DCO(F8, 0.100f, 1.000f, PWM_MAN,
        ON , ON , ON , 0.700f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.000f, 0.400f, POL_NEG, 0.000f, 0.700f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.500f, 0.000f, 0.500f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Effect Sound II", {
    LFO(0.500f, 0.900f),
    DCO(F8, 0.000f, 0.300f, PWM_LFO,
        ON , OFF, ON , 0.600f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.300f, 0.400f, POL_NEG, 0.000f, 0.100f),
    VCA(0.700f, VCA_GATE),
    ENV(0.600f, 0.500f, 0.200f, 0.600f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Harm", {
    LFO(0.500f, 0.000f),
    DCO(F8, 0.200f, 0.000f, PWM_ENV,
        ON , OFF, OFF, 0.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.500f, 0.500f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 0.800f, 0.800f, 0.900f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Funk", {
    LFO(0.300f, 0.200f),
    DCO(F8, 0.000f, 0.600f, PWM_MAN,
        ON , ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.700f, 0.600f, 0.500f, POL_NEG, 0.000f, 0.400f),
    VCA(0.700f, VCA_GATE),
    ENV(0.600f, 0.500f, 0.000f, 0.000f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Sound I", {
    LFO(0.600f, 0.700f),
    DCO(F8, 0.200f, 0.400f, PWM_MAN,
        OFF, ON , ON , 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.600f, 0.700f, 0.500f, POL_NEG, 0.000f, 1.000f),
    VCA(0.700f, VCA_GATE),
    ENV(0.000f, 0.800f, 0.000f, 0.300f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

{ "Mysterious Inve", {
    LFO(0.600f, 0.800f),
    DCO(F8, 0.200f, 0.800f, PWM_ENV,
        ON , ON , OFF, 1.000f, 0.000f),
    HPF_(R4(0)),
    VCF(0.800f, 0.700f, 0.600f, POL_NEG, 0.200f, 0.000f),
    VCA(0.700f, VCA_ENV),
    ENV(0.000f, 1.000f, 0.000f, 1.000f),
    CHOR(CH_OFF),
    SYS(TRIG_AUTO, OCT0) }},

{ "Space Sound II", {
    LFO(0.300f, 0.300f),
    DCO(F8, 0.000f, 0.600f, PWM_MAN,
        ON , ON , OFF, 0.800f, 0.000f),
    HPF_(R4(0)),
    VCF(0.200f, 0.800f, 0.600f, POL_POS, 0.000f, 1.000f),
    VCA(0.700f, VCA_ENV),
    ENV(1.000f, 1.000f, 1.000f, 1.000f),
    CHOR(CH_I),
    SYS(TRIG_AUTO, OCT0) }},

};

static_assert(sizeof(junoPrograms) / sizeof(junoPrograms[0]) == JUNO_NPROGRAMS,
              "patch table and JUNO_NPROGRAMS have drifted apart");
