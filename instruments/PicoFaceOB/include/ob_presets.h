// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ob_presets.h - factory patches converted from OB-Xf.
//
// Source: assets/installer/Surge Synth Team/OB-Xf/Patches in
// https://github.com/surge-synthesizer/OB-Xf (GPL-3.0-or-later). The upstream
// .fxp files carry their parameters as named, normalised values in an
// embedded XML blob, so the conversion is a straight name-to-name mapping
// onto ob_params.h. Parameters this port does not have (unison, panning,
// LFO 2, the modulation matrix, velocity tracking) are dropped, and the LFO
// waveform blend is snapped to the nearest of our five fixed positions - a
// patch that leaned on those will not sound identical.
//
// Generated, do not edit by hand.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).
#ifndef OB_PRESETS_H
#define OB_PRESETS_H

#include "ob_params.h"

struct ObPreset
{
    const char* name;
    float       value[OB_PARAM_COUNT];
};

inline const ObPreset obPresets[] = {
    // Basses / '32 Rez Bass
    {"'32 Rez Bass", {
        1.0000f, 1.0000f, 0.5640f, 1.0000f, 0.0000f, 1.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.1200f, 0.0000f, 0.0000f, 0.0000f, 1.0000f, 0.2960f, 0.7000f, 0.0000f, 0.5200f, 0.0000f, 0.0000f, 1.0000f, 0.0000f, 0.9520f, 0.0000f, 0.7480f, 0.0000f, 0.0000f, 1.0000f, 0.3960f, 0.5000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.2080f, 1.0000f, 0.1040f}},
    // Basses / A Modern Day Warrior
    {"A Modern Day Wa", {
        1.0000f, 1.0000f, 0.0000f, 1.0000f, 0.0000f, 1.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.5000f, 0.5000f, 1.0000f, 0.2333f, 1.0000f, 0.0000f, 0.7000f, 0.0000f, 0.0000f, 0.0000f, 0.0147f, 0.8062f, 0.0000f, 0.8388f, 0.0000f, 0.8280f, 1.0000f, 0.4064f, 0.5000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.1000f, 0.7500f, 0.1500f}},
    // Brass / Accented
    {"Accented", {
        1.0000f, 1.0000f, 0.3440f, 1.0000f, 0.0000f, 1.0000f, 1.0000f, 0.0000f, 1.0000f, 0.6520f, 0.0320f, 0.0000f, 0.2500f, 0.2520f, 1.0000f, 0.0320f, 0.0040f, 1.0000f, 0.7440f, 1.0000f, 0.0000f, 0.0000f, 0.1320f, 0.2320f, 0.5520f, 0.4160f, 0.0000f, 0.5360f, 0.7320f, 0.2640f, 0.4400f, 0.2500f, 0.0520f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.1720f}},
    // Brass / Brass Section Chords
    {"Brass Section C", {
        1.0000f, 1.0000f, 0.0097f, 0.3819f, 0.2587f, 0.8508f, 1.0000f, 0.0000f, 0.8335f, 0.9048f, 0.7200f, 1.0000f, 0.5000f, 0.4880f, 0.0273f, 0.4024f, 0.2300f, 0.3511f, 0.0268f, 0.5738f, 0.9017f, 0.1814f, 0.1400f, 0.1653f, 0.8558f, 0.3888f, 0.1454f, 0.2983f, 0.1280f, 0.3394f, 0.2365f, 0.2500f, 0.0000f, 0.0000f, 0.0000f, 0.2727f, 0.1345f, 0.4000f}},
    // Keys / Accordion 1
    {"Accordion 1", {
        1.0000f, 0.7160f, 0.2480f, 0.0000f, 1.0000f, 0.0000f, 1.0000f, 0.2280f, 0.0000f, 0.0000f, 0.0000f, 0.6560f, 0.5000f, 0.7500f, 1.0000f, 0.4120f, 0.0360f, 1.0000f, 0.4440f, 1.0000f, 0.0000f, 1.0000f, 0.0840f, 0.0000f, 1.0000f, 0.5760f, 0.0760f, 0.0000f, 1.0000f, 0.2640f, 0.5040f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.3920f, 0.3000f}},
    // Leads / 2077 Lead
    {"2077 Lead", {
        0.7760f, 1.0000f, 0.0480f, 0.0000f, 1.0000f, 1.0000f, 0.0000f, 0.4440f, 0.0000f, 0.1800f, 0.0000f, 0.2360f, 0.4480f, 0.5000f, 1.0000f, 0.5640f, 0.2360f, 1.0000f, 0.0760f, 0.6600f, 0.4840f, 0.0000f, 0.3840f, 0.7800f, 0.0000f, 0.6600f, 0.0920f, 0.7200f, 0.6880f, 0.7160f, 0.0360f, 0.0000f, 0.1400f, 0.7400f, 0.0000f, 0.6720f, 0.2500f, 0.3000f}},
    // Leads / 8VM Lead
    {"8VM Lead", {
        1.0000f, 0.7943f, 0.0991f, 1.0000f, 0.0000f, 1.0000f, 1.0000f, 0.3495f, 0.0000f, 0.0000f, 0.0000f, 0.6310f, 0.5000f, 0.7500f, 0.1500f, 0.7841f, 0.1000f, 1.0000f, 0.1000f, 0.0000f, 0.0000f, 1.0000f, 0.0186f, 0.0000f, 1.0000f, 0.2286f, 0.0000f, 0.0000f, 1.0000f, 0.2835f, 0.5000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.5000f, 0.2500f, 0.5000f}},
    // Organs / Carborgen
    {"Carborgen", {
        1.0000f, 0.9360f, 0.0000f, 0.0000f, 1.0000f, 0.0000f, 1.0000f, 0.3320f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.5000f, 0.8960f, 1.0000f, 0.4954f, 1.0000f, 0.0000f, 0.1640f, 1.0000f, 0.2920f, 1.0000f, 0.0000f, 0.0640f, 0.0000f, 0.0000f, 0.0000f, 0.0720f, 0.3680f, 0.1320f, 0.5440f, 0.0000f, 0.0000f, 0.8080f, 0.3640f, 0.0000f, 0.0000f, 0.4500f}},
    // Pads / 5 AM Pad
    {"5 AM Pad", {
        1.0000f, 1.0000f, 0.0000f, 1.0000f, 0.0000f, 0.0000f, 1.0000f, 1.0000f, 0.0000f, 0.0000f, 0.1800f, 0.0000f, 0.5000f, 0.5000f, 1.0000f, 0.0000f, 0.0320f, 1.0000f, 0.4200f, 0.2360f, 0.0000f, 0.0000f, 0.2720f, 1.0000f, 1.0000f, 1.0000f, 0.1760f, 1.0000f, 1.0000f, 0.9600f, 0.2560f, 0.0000f, 0.0720f, 0.0000f, 0.0000f, 0.0000f, 0.4620f, 0.5000f}},
    // Pads / Astheas
    {"Astheas", {
        0.6310f, 0.6310f, 0.0000f, 1.0000f, 1.0000f, 0.9950f, 1.0000f, 0.3509f, 1.0000f, 0.5000f, 0.0000f, 0.4467f, 0.5000f, 0.2500f, 0.5000f, 0.2840f, 0.1000f, 1.0000f, 0.5000f, 0.2500f, 0.0000f, 0.0050f, 0.4640f, 0.5919f, 0.3333f, 0.4639f, 0.5560f, 0.6240f, 0.2500f, 0.5042f, 0.5000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.2500f, 0.5000f}},
    // Strings / Attack Strings
    {"Attack Strings", {
        1.0000f, 1.0000f, 0.3440f, 1.0000f, 0.0000f, 0.0000f, 1.0000f, 0.0000f, 0.0000f, 0.0480f, 0.0000f, 0.1400f, 0.5000f, 0.5000f, 1.0000f, 0.7920f, 0.0240f, 1.0000f, 0.3800f, 0.2680f, 0.0000f, 0.0000f, 0.0000f, 0.1800f, 0.0000f, 0.2600f, 0.1640f, 0.2400f, 0.2160f, 0.5040f, 0.4400f, 0.2500f, 0.2040f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.4000f}},
    // Plucks / Acidisillification
    {"Acidisillificat", {
        1.0000f, 0.6520f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 1.0000f, 0.0000f, 1.0000f, 0.2280f, 0.0000f, 0.2760f, 0.5000f, 0.3880f, 1.0000f, 0.4240f, 0.0000f, 0.0000f, 0.6160f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.4920f, 0.0000f, 0.2200f, 0.0000f, 0.4280f, 0.0000f, 0.2920f, 0.5000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.2500f, 0.5000f}},
};

inline constexpr int OB_NPRESETS = (int)(sizeof(obPresets) / sizeof(obPresets[0]));

#endif // OB_PRESETS_H
