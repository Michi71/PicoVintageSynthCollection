// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_presets.cpp -- factory panel settings

  Each entry is a complete front panel, written in the order of the enum in
  moog_params.h and grouped by the five sections of the instrument. The macros
  are there to make the grouping visible and, more usefully, to make a
  miscounted row a compile error rather than a preset with a silently zeroed
  tail: CTL takes six arguments, OSCS eight, MIX eleven, MODF thirteen,
  OUTV six and FX fourteen, and together they expand to exactly
  MOOG_PARAM_COUNT values.

  Positions of rotary switches are written as R6(3) and so on -- the middle of
  the slot, so that a value surviving a round trip through the per mille IPC
  or the settings record cannot land one position over.
*/

#include "moog/moog_presets.h"

/* Middle of slot i of an n-position switch. */
#define R2(i)  (((float) (i) + 0.5f) /  2.0f)
#define R3(i)  (((float) (i) + 0.5f) /  3.0f)
#define R4(i)  (((float) (i) + 0.5f) /  4.0f)
#define R5(i)  (((float) (i) + 0.5f) /  5.0f)
#define R6(i)  (((float) (i) + 0.5f) /  6.0f)
#define R13(i) (((float) (i) + 0.5f) / 13.0f)

#define OFF 0.0f
#define ON  1.0f

/* Range switch */
#define LO   R6(0)
#define F32  R6(1)
#define F16  R6(2)
#define F8   R6(3)
#define F4   R6(4)
#define F2   R6(5)

/* Waveform switch. W_ALT is position 2: sawtooth-triangular on oscillators
 * 1 and 2, reverse sawtooth on oscillator 3. */
#define W_TRI R6(0)
#define W_ALT R6(1)
#define W_SAW R6(2)
#define W_SQR R6(3)
#define W_WID R6(4)
#define W_NAR R6(5)

#define WHITE R2(0)
#define PINK  R2(1)

#define PRIO_LOW  R3(0)
#define PRIO_HIGH R3(1)
#define PRIO_LAST R3(2)

#define TRIG_SINGLE R2(0)
#define TRIG_MULTI  R2(1)

#define OCT0  R5(2)      /* no transposition */
#define BEND2 R13(2)     /* two semitones, what a MIDI controller expects */

/* Controllers. Tune always sits at the detent -- a preset that arrives
 * out of tune is a fault, not a sound. */
#define CTL(glide, glideSw, modMix, modWhl, oscMod, osc3Kbd) \
    0.5f, glide, glideSw, modMix, modWhl, oscMod, osc3Kbd, BEND2

/* Oscillator bank */
#define OSCS(r1, w1, r2, f2, w2, r3, f3, w3) \
    r1, w1, r2, f2, w2, r3, f3, w3

/* Mixer */
#define MIX(v1, o1, v2, o2, v3, o3, vn, on, colour, vfb, ofb) \
    v1, o1, v2, o2, v3, o3, vn, on, colour, vfb, ofb

/* Modifiers: filter, then its contour, then the loudness contour */
#define MODF(cut, emph, contour, fmod, kb1, kb2, fa, fd, fs, la, ld, ls, dsw) \
    cut, emph, contour, fmod, kb1, kb2, fa, fd, fs, la, ld, ls, dsw

/* Effects. No counterpart on the instrument, so the presets leave both slots
 * empty and the section costs nothing until someone switches it on. FXOFF is
 * that state; FX() spells out a preset that does use them, which of the
 * twenty-five is only "Shine On". */
#define FX(slotA, slotB, cRate, cDepth, cMix, cFb, dTime, dFb, dMix, dTone, \
           rSize, rDamp, rMix, rWidth) \
    slotA, slotB, cRate, cDepth, cMix, cFb, dTime, dFb, dMix, dTone, \
    rSize, rDamp, rMix, rWidth

#define FX_NONE   R4(0)
#define FX_CHORUS R4(1)
#define FX_DELAY  R4(2)
#define FX_REVERB R4(3)

/* Slots empty, but with settings that already sound reasonable the moment a
 * slot is filled -- an effect that arrives silent looks broken. */
#define FXOFF \
    FX(FX_NONE, FX_NONE,  0.35f, 0.50f, 0.50f, 0.00f, \
                          0.45f, 0.35f, 0.30f, 0.60f, \
                          0.60f, 0.35f, 0.30f, 1.00f)


/* Output and voicing. The A-440 tone is never on in a preset. */
#define OUTV(vol, drive, drift, tone, prio, trig) \
    vol, OFF, drive, drift, tone, prio, trig, OCT0

const MoogProgram moogPrograms[MOOG_NPROGRAMS] = {

{ "Fat Bass", {
    CTL(0.18f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F16, W_SAW,  F16, 0.530f, W_SAW,  F32, 0.470f, W_SAW),
    MIX(0.85f, ON, 0.75f, ON, 0.60f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.30f, 0.42f, 0.55f, OFF, ON, OFF,
         0.02f, 0.30f, 0.10f,  0.02f, 0.35f, 0.85f, ON),
    OUTV(0.80f, 0.45f, 0.35f, 0.75f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Lead Solo", {
    CTL(0.20f, ON, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SAW,  F8, 0.520f, W_SAW,  F8, 0.480f, W_TRI),
    MIX(0.80f, ON, 0.75f, ON, 0.45f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.45f, 0.55f, 0.40f, OFF, ON, ON,
         0.05f, 0.40f, 0.35f,  0.03f, 0.45f, 0.90f, ON),
    OUTV(0.78f, 0.40f, 0.40f, 0.80f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Taurus Bass", {
    CTL(0.16f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F32, W_SAW,  F32, 0.505f, W_SAW,  F16, 0.500f, W_SAW),
    MIX(0.90f, ON, 0.70f, ON, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.22f, 0.25f, 0.35f, OFF, ON, OFF,
         0.02f, 0.45f, 0.30f,  0.02f, 0.50f, 0.95f, ON),
    OUTV(0.85f, 0.50f, 0.25f, 0.60f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Brass", {
    CTL(0.20f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SAW,  F8, 0.540f, W_SAW,  F16, 0.500f, W_SAW),
    MIX(0.75f, ON, 0.70f, ON, 0.55f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.38f, 0.35f, 0.45f, OFF, ON, ON,
         0.28f, 0.45f, 0.55f,  0.20f, 0.45f, 0.85f, ON),
    OUTV(0.75f, 0.35f, 0.35f, 0.82f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Funk Bass", {
    CTL(0.14f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F16, W_NAR,  F16, 0.515f, W_SAW,  F8, 0.500f, W_NAR),
    MIX(0.80f, ON, 0.65f, ON, 0.30f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.25f, 0.68f, 0.55f, OFF, ON, OFF,
         0.01f, 0.22f, 0.05f,  0.01f, 0.28f, 0.60f, OFF),
    OUTV(0.80f, 0.55f, 0.30f, 0.78f, PRIO_LOW, TRIG_MULTI),
    FXOFF }},

{ "Flute", {
    CTL(0.22f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_TRI,  F8, 0.500f, W_TRI,  F16, 0.500f, W_TRI),
    MIX(0.85f, ON, 0.00f, OFF, 0.00f, OFF, 0.10f, ON, PINK, 0.00f, OFF),
    MODF(0.62f, 0.15f, 0.20f, OFF, ON, ON,
         0.20f, 0.40f, 0.70f,  0.22f, 0.40f, 0.90f, ON),
    OUTV(0.72f, 0.20f, 0.45f, 0.70f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

/* Oscillator 3 off the keyboard and down in LO, pointed at the pitch of the
 * other two: the vibrato of the instrument, with the wheel already up a
 * little so the preset shows what it does. */
{ "Whistle", {
    CTL(0.15f, ON, 0.00f, 0.167f, ON, OFF),
    OSCS(F4, W_TRI,  F4, 0.500f, W_TRI,  LO, 0.327f, W_TRI),
    MIX(0.80f, ON, 0.00f, OFF, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.70f, 0.30f, 0.15f, OFF, ON, ON,
         0.15f, 0.40f, 0.80f,  0.18f, 0.40f, 0.92f, ON),
    OUTV(0.70f, 0.20f, 0.40f, 0.85f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "String Pad", {
    CTL(0.30f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SAW,  F8, 0.545f, W_SAW,  F16, 0.455f, W_SAW),
    MIX(0.70f, ON, 0.70f, ON, 0.60f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.40f, 0.30f, 0.35f, OFF, ON, ON,
         0.45f, 0.55f, 0.60f,  0.42f, 0.60f, 0.88f, ON),
    OUTV(0.72f, 0.30f, 0.50f, 0.78f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Hard Lead", {
    CTL(0.18f, ON, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_NAR,  F8, 0.560f, W_SAW,  F4, 0.500f, W_WID),
    MIX(0.85f, ON, 0.80f, ON, 0.50f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.42f, 0.72f, 0.45f, OFF, ON, ON,
         0.03f, 0.35f, 0.40f,  0.02f, 0.40f, 0.92f, ON),
    OUTV(0.80f, 0.60f, 0.35f, 0.85f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

/* Modulation mix into the filter instead of the pitch: switch (J) on, the
 * wheel already past halfway. */
{ "Wobble Bass", {
    CTL(0.16f, OFF, 0.00f, 0.742f, OFF, OFF),
    OSCS(F16, W_SAW,  F16, 0.520f, W_SAW,  LO, 0.185f, W_TRI),
    MIX(0.85f, ON, 0.70f, ON, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.22f, 0.70f, 0.20f, ON, ON, OFF,
         0.02f, 0.40f, 0.50f,  0.02f, 0.45f, 0.95f, ON),
    OUTV(0.82f, 0.50f, 0.30f, 0.75f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Vibrato Lead", {
    CTL(0.20f, ON, 0.00f, 0.205f, ON, OFF),
    OSCS(F8, W_SAW,  F8, 0.530f, W_ALT,  LO, 0.273f, W_TRI),
    MIX(0.80f, ON, 0.70f, ON, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.48f, 0.45f, 0.35f, OFF, ON, ON,
         0.10f, 0.40f, 0.55f,  0.06f, 0.45f, 0.90f, ON),
    OUTV(0.78f, 0.38f, 0.40f, 0.82f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Percussion", {
    CTL(0.12f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SQR,  F4, 0.530f, W_SQR,  F2, 0.500f, W_SQR),
    MIX(0.70f, ON, 0.50f, ON, 0.30f, ON, 0.25f, ON, WHITE, 0.00f, OFF),
    MODF(0.30f, 0.60f, 0.60f, OFF, ON, ON,
         0.00f, 0.18f, 0.00f,  0.00f, 0.20f, 0.00f, OFF),
    OUTV(0.80f, 0.45f, 0.30f, 0.85f, PRIO_LOW, TRIG_MULTI),
    FXOFF }},

{ "Snare", {
    CTL(0.12f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_TRI,  F8, 0.500f, W_TRI,  F8, 0.500f, W_TRI),
    MIX(0.15f, ON, 0.00f, OFF, 0.00f, OFF, 0.85f, ON, WHITE, 0.00f, OFF),
    MODF(0.55f, 0.35f, 0.30f, OFF, OFF, OFF,
         0.00f, 0.15f, 0.00f,  0.00f, 0.18f, 0.00f, OFF),
    OUTV(0.78f, 0.30f, 0.20f, 0.90f, PRIO_LOW, TRIG_MULTI),
    FXOFF }},

/* Noise rather than oscillator 3 as the modulation source, aimed at the
 * filter: the modulation mix turned all the way over. */
{ "Wind", {
    CTL(0.30f, OFF, 1.00f, 0.592f, OFF, OFF),
    OSCS(F8, W_TRI,  F8, 0.500f, W_TRI,  LO, 0.109f, W_TRI),
    MIX(0.00f, OFF, 0.00f, OFF, 0.00f, OFF, 0.90f, ON, PINK, 0.00f, OFF),
    MODF(0.35f, 0.55f, 0.25f, ON, OFF, OFF,
         0.55f, 0.60f, 0.70f,  0.50f, 0.60f, 0.90f, ON),
    OUTV(0.70f, 0.25f, 0.30f, 0.60f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Organ", {
    CTL(0.15f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F16, W_SQR,  F8, 0.500f, W_SQR,  F4, 0.500f, W_SQR),
    MIX(0.70f, ON, 0.60f, ON, 0.45f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.58f, 0.20f, 0.00f, OFF, ON, ON,
         0.00f, 0.30f, 1.00f,  0.01f, 0.30f, 1.00f, OFF),
    OUTV(0.75f, 0.30f, 0.15f, 0.80f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Clav", {
    CTL(0.12f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_NAR,  F8, 0.518f, W_NAR,  F16, 0.500f, W_SAW),
    MIX(0.80f, ON, 0.60f, ON, 0.25f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.32f, 0.62f, 0.50f, OFF, ON, ON,
         0.00f, 0.20f, 0.05f,  0.00f, 0.25f, 0.30f, OFF),
    OUTV(0.80f, 0.50f, 0.28f, 0.88f, PRIO_LOW, TRIG_MULTI),
    FXOFF }},

{ "Sub Bass", {
    CTL(0.18f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F32, W_TRI,  F32, 0.510f, W_SAW,  F16, 0.500f, W_TRI),
    MIX(0.90f, ON, 0.40f, ON, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.18f, 0.10f, 0.25f, OFF, ON, OFF,
         0.02f, 0.45f, 0.40f,  0.02f, 0.50f, 0.95f, ON),
    OUTV(0.88f, 0.35f, 0.20f, 0.55f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

/* The output patched back into the external input, which is what the
 * feedback control is. Together with a high Drive this is where the
 * instrument stops sounding polite. */
{ "Growl Bass", {
    CTL(0.18f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F16, W_SAW,  F16, 0.535f, W_WID,  F32, 0.500f, W_SAW),
    MIX(0.90f, ON, 0.85f, ON, 0.70f, ON, 0.00f, OFF, WHITE, 0.45f, ON),
    MODF(0.26f, 0.55f, 0.50f, OFF, ON, OFF,
         0.02f, 0.35f, 0.20f,  0.02f, 0.40f, 0.88f, ON),
    OUTV(0.78f, 0.75f, 0.40f, 0.72f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Reso Sweep", {
    CTL(0.25f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SAW,  F16, 0.500f, W_SAW,  F8, 0.500f, W_SAW),
    MIX(0.75f, ON, 0.65f, ON, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.12f, 0.85f, 0.75f, OFF, ON, OFF,
         0.60f, 0.65f, 0.30f,  0.10f, 0.60f, 0.90f, ON),
    OUTV(0.72f, 0.40f, 0.35f, 0.80f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Bell", {
    CTL(0.15f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F4, W_TRI,  F2, 0.620f, W_TRI,  F8, 0.380f, W_SQR),
    MIX(0.70f, ON, 0.55f, ON, 0.35f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.60f, 0.40f, 0.35f, OFF, ON, ON,
         0.00f, 0.35f, 0.10f,  0.00f, 0.42f, 0.15f, ON),
    OUTV(0.75f, 0.25f, 0.30f, 0.90f, PRIO_LOW, TRIG_MULTI),
    FXOFF }},

{ "Space Drone", {
    CTL(0.40f, OFF, 0.30f, 0.490f, ON, OFF),
    OSCS(F16, W_SAW,  F8, 0.545f, W_ALT,  LO, 0.055f, W_TRI),
    MIX(0.70f, ON, 0.65f, ON, 0.00f, OFF, 0.15f, ON, PINK, 0.00f, OFF),
    MODF(0.35f, 0.60f, 0.30f, ON, ON, OFF,
         0.70f, 0.70f, 0.60f,  0.60f, 0.75f, 0.90f, ON),
    OUTV(0.70f, 0.35f, 0.60f, 0.70f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Trumpet", {
    CTL(0.20f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_WID,  F8, 0.525f, W_SAW,  F16, 0.500f, W_SAW),
    MIX(0.80f, ON, 0.60f, ON, 0.35f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.36f, 0.45f, 0.50f, OFF, ON, ON,
         0.18f, 0.42f, 0.50f,  0.12f, 0.42f, 0.85f, ON),
    OUTV(0.76f, 0.42f, 0.35f, 0.84f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

{ "Detune Stack", {
    CTL(0.22f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SAW,  F8, 0.580f, W_SAW,  F8, 0.420f, W_SAW),
    MIX(0.80f, ON, 0.80f, ON, 0.80f, ON, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.44f, 0.35f, 0.35f, OFF, ON, ON,
         0.10f, 0.45f, 0.55f,  0.08f, 0.50f, 0.90f, ON),
    OUTV(0.74f, 0.45f, 0.55f, 0.80f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

/* Straight out of the manual: "When the EMPHASIS control is set to 10, the
 * filter breaks into oscillation, and produces a pure sine wave tone. It is
 * thus available as a sixth sound source." All mixer switches off, both
 * keyboard control switches on so it plays in tune. */
{ "Sixth Source", {
    CTL(0.20f, OFF, 0.00f, 0.00f, OFF, ON),
    OSCS(F8, W_SAW,  F8, 0.500f, W_SAW,  F8, 0.500f, W_SAW),
    MIX(0.00f, OFF, 0.00f, OFF, 0.00f, OFF, 0.00f, OFF, WHITE, 0.00f, OFF),
    MODF(0.25f, 1.00f, 0.00f, OFF, ON, ON,
         0.00f, 0.40f, 1.00f,  0.02f, 0.40f, 0.95f, ON),
    OUTV(0.70f, 0.20f, 0.25f, 0.85f, PRIO_LOW, TRIG_SINGLE),
    FXOFF }},

/*
 * The four-note theme. Two sawtooths an inch apart in tuning, a filter kept
 * fairly closed with just enough emphasis to go nasal, and oscillator 3 down
 * in LO doing nothing but vibrato.
 *
 * Values from the patch sheet, converted to the panel scale:
 *   Glide switch off (see below)         Emphasis 3..4   -> 0.35
 *   Modulation Mix 50 %                 Loudness env 20 ms / 200 ms / 10
 *   Osc 1/2 8' sawtooth, volume 8       Decay switch on
 *   Osc 2 detuned +4 cents              External input at 3.5 (the feedback
 *   Osc 3 LO triangle, keyboard off     trick, listed as optional -- it is
 *                                       where the warmth comes from)
 *
 * The filter contour departs from the sheet, deliberately. Its 200 ms attack
 * and 500 ms decay put a slow sweep across the front of every note: measured,
 * the spectral centroid climbed from 246 to 542 Hz over 230 ms and then fell
 * back to 449 Hz. That reads as a growl, and this part wants a horn -- one
 * that speaks at once and then holds still.
 *
 * So: attack 25 ms, decay 150 ms, sustain 8.5, and the cutoff dropped from
 * -0.5 to -0.9 to pay for the higher sustain. The steady-state cutoff comes
 * out at 788 Hz either way, so the colour is the one the sheet asks for; what
 * changed is that the contour now puts a 380 cent blip on the attack instead
 * of a 700 ms arc. Measured again, the centroid settles within 46 ms and then
 * stays between 420 and 450 Hz.
 *
 * Two things the sheet does not give a number for:
 *
 *   The oscillator 3 frequency. It only says the range switch is on LO, which
 *   spans 0.36..16.3 Hz here. 0.689 puts it at 5.00 Hz, a normal vibrato rate.
 *
 *   That value used to be 0.882. It moved when the LO range was corrected
 *   against the schematic -- LO turned out to sit 6.75 octaves under 8', not
 *   the 8.0 this engine had assumed, so every setting of this control came
 *   out 1.25 octaves fast and the vibrato here ran at 10.4 Hz. Five other
 *   presets that use oscillator 3 as an LFO moved with it. The rate is what
 *   was authored; only the knob position that reaches it has changed.
 *
 *   The modulation wheel. On the instrument this is played, not set -- you
 *   bring the vibrato in by hand on the long notes, and it is not on for the
 *   whole piece. A preset has to pick something, and this ships at 0.020,
 *   which is about +/-10 cents from oscillator 3: a shimmer that is there
 *   without the note sounding as though it were sobbing. Raise it with CC 1
 *   or on the CTL MOD page for as much wail as the part wants.
 *
 *   It shipped at 0.050 first, which is +/-26 cents of 5 Hz vibrato on every
 *   note, and that is a lot on a sustained lead. Note that the Modulation Mix
 *   is not what makes that dirty: measured at equal depth, oscillator 3
 *   smears the spectrum to 20 dB harmonic-to-rest while the noise source
 *   only reaches 46 dB. Turn Mod Mix to 0 for a clean sine vibrato, but
 *   expect it to get more prominent, not less.
 *
 * The sheet asks for Glide 2..3 with the switch on. At 270 ms that slurred
 * between every note of the theme, and even at 30 ms it made the line
 * restless, so the switch ships off. The time is left at 1 rather than zero
 * on purpose: a switch with nothing behind it looks broken, and this way
 * turning it on gives a short portamento straight away.
 *
 * Single trigger matters here more than anywhere else in the list: the theme
 * is played legato, and the contours must not restart between the notes.
 */
{ "Shine On", {
    CTL(0.10f, OFF, 0.50f, 0.141f, ON, OFF),
    OSCS(F8, W_SAW,  F8, 0.5029f, W_SAW,  LO, 0.689f, W_TRI),
    MIX(0.80f, ON, 0.80f, ON, 0.00f, OFF, 0.00f, OFF, WHITE, 0.35f, ON),
    MODF(0.385f, 0.35f, 0.35f, OFF, ON, OFF,
         0.133f, 0.392f, 0.85f,  0.100f, 0.434f, 1.00f, ON),
    OUTV(0.78f, 0.40f, 0.35f, 0.78f, PRIO_LOW, TRIG_SINGLE),
    /* The one preset that ships with its slots filled. Delay into reverb, in
     * that order, so the repeats are what gets reverberated rather than the
     * other way round: 430 ms with the repeats darkening as they go, under a
     * hall that is there without swallowing the line. */
    FX(FX_DELAY, FX_REVERB,  0.35f, 0.50f, 0.50f, 0.00f,
                             0.555f, 0.38f, 0.28f, 0.55f,
                             0.65f, 0.40f, 0.25f, 1.00f) }},

};

static_assert(sizeof(moogPrograms) / sizeof(moogPrograms[0]) == MOOG_NPROGRAMS,
              "preset table and MOOG_NPROGRAMS have drifted apart");
