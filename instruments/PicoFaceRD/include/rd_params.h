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
 * LFO rate laws, and the grid the panel steps them on.
 *
 * The three rate encoders move in exact 0.05 Hz detents, so the display always
 * reads a round figure -- editing used to land on values like 3.04 Hz because
 * the encoder stepped one percent of an arbitrary span. The stored value IS the
 * grid index (Hz * 20), which is why these ranges are stated as indices: the
 * panel counts grid positions and the display just multiplies by 0.05.
 *
 * Chorus and tremolo are measured. Both service manuals tabulate the LFO period
 * at a test point for all fifteen settings (MKS-20 p.7 at CP3 and CP4, MK-80
 * the same): chorus 2700..175 ms is 0.370..5.714 Hz, tremolo 2100..130 ms is
 * 0.476..7.692 Hz, each linear in the setting to within 3.7 % and 5.2 %. The
 * endpoints here are those figures snapped to the grid -- 0.35..5.70 and
 * 0.50..7.70 -- which moves them by at most 5 % at the bottom and 0.2 % at the
 * top, comfortably inside the scatter of the manuals' own tables.
 *
 * The phaser is not in either table; it is an MK-80 effect and its range is
 * ours. It used to be an exponential decade and a half and is linear now, for
 * the sake of the same grid. If the slow end turns out too coarse that is the
 * thing to revisit.
 */
#define RD_RATE_STEP_HZ 0.05f

// Grid index bounds, in units of 0.05 Hz.
#define RD_CHORUS_IDX_MIN   7u    // 0.35 Hz
#define RD_CHORUS_IDX_MAX 114u    // 5.70 Hz
#define RD_TREM_IDX_MIN    10u    // 0.50 Hz
#define RD_TREM_IDX_MAX   154u    // 7.70 Hz
#define RD_PHASER_IDX_MIN   2u    // 0.10 Hz
#define RD_PHASER_IDX_MAX 100u    // 5.00 Hz

static inline bool rd_is_rate_param(uint8_t id)
{
    return id == RD_PARAM_CHORUS_RATE || id == RD_PARAM_TREM_RATE || id == RD_PARAM_PHASER_RATE;
}

static inline uint8_t rd_rate_idx_min(uint8_t id)
{
    if (id == RD_PARAM_CHORUS_RATE) return RD_CHORUS_IDX_MIN;
    if (id == RD_PARAM_TREM_RATE)   return RD_TREM_IDX_MIN;
    return RD_PHASER_IDX_MIN;
}

static inline uint8_t rd_rate_idx_max(uint8_t id)
{
    if (id == RD_PARAM_CHORUS_RATE) return RD_CHORUS_IDX_MAX;
    if (id == RD_PARAM_TREM_RATE)   return RD_TREM_IDX_MAX;
    return RD_PHASER_IDX_MAX;
}

// What the display prints. The grid index is the value, so this is exact.
static inline float rd_rate_hz_from_idx(uint8_t idx) { return (float)idx * RD_RATE_STEP_HZ; }

// What the engine computes from the 0..1 it receives over the wire. Endpoints
// match the index bounds above, so the two agree to better than half a step.
static inline float rd_chorus_rate_hz(float x01) { return 0.35f + 5.35f * x01; }
static inline float rd_trem_rate_hz  (float x01) { return 0.50f + 7.20f * x01; }
static inline float rd_phaser_rate_hz(float x01) { return 0.10f + 4.90f * x01; }

#endif // RD_PARAMS_H
