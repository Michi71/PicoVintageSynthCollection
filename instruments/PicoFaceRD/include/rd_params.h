// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef RD_PARAMS_H
#define RD_PARAMS_H

#include <cmath>

#include <stdint.h>

// How many patches this build ships, and which patch number it starts at.
// PicoFaceRD normally carries both machines -- sixteen patches, and a 16 MB
// board to hold them. A single-machine build ships half the packs and drops
// the sample banks the other machine used, which fits a 4 MB Pico 2; see
// instruments/PicoFaceRD/instrument.cmake. Either way the firmware counts
// 0..RD_PATCH_COUNT-1, and RD_PATCH_BASE turns that into the number the
// machine itself uses, which is what indexes the name and rate tables.
#ifndef RD_PATCH_COUNT
#define RD_PATCH_COUNT 16
#endif
#ifndef RD_PATCH_BASE
#define RD_PATCH_BASE 0
#endif

/*
 * shared parameter IDs between UI controller on Core 1 and audio engine on Core 0 of a Roland MKS-20/MK-80 emulation;
 * values travel over IPC as uint16 0..255 and are normalized to float 0..1 as val/255.0f on the receiving side;
 * RD_PARAM_INSTRUMENT=0x7F lives separately in RD_Midi.h
 */
enum RdParamId : uint8_t { RD_PARAM_VOLUME=0, RD_PARAM_CHORUS_ON, RD_PARAM_CHORUS_RATE, RD_PARAM_CHORUS_DEPTH, RD_PARAM_TREM_ON, RD_PARAM_TREM_RATE, RD_PARAM_TREM_DEPTH, RD_PARAM_BASS, RD_PARAM_TREBLE, RD_PARAM_DAC_FILTER_ON, RD_PARAM_PHASER_ON, RD_PARAM_PHASER_RATE, RD_PARAM_PHASER_DEPTH, RD_PARAM_VOICE_MODE, RD_PARAM_MASTER_TUNE, RD_PARAM_COUNT };

/*
 * LFO rate laws, in Hz, for a 0..1 setting. One place, because the audio engine
 * sets the LFOs from these and the display prints them -- and a display that
 * disagrees with the thing it is describing is worse than one that says nothing.
 *
 * Chorus and tremolo are measured: both service manuals tabulate the LFO period
 * at a test point for all fifteen settings (MKS-20 p.7 at CP3 and CP4, MK-80 the
 * same). Chorus 2700..175 ms is 0.370..5.714 Hz, linear in the setting to within
 * 3.7 %; tremolo 2100..130 ms is 0.476..7.692 Hz, linear to within 5.2 %. The
 * phaser is not in either table -- it is an MK-80 effect and its range here is
 * ours, an exponential decade and a half.
 */
static inline float rd_chorus_rate_hz(float x01) { return 0.370f + 5.344f * x01; }
static inline float rd_trem_rate_hz  (float x01) { return 0.476f + 7.224f * x01; }
static inline float rd_phaser_rate_hz(float x01) { return 0.1f * powf(10.0f, 1.69897000433601880479f * x01); }

#endif // RD_PARAMS_H
