// include/presets.h
//
// Built-in DX preset table: the 32 real Yamaha reface DX factory-bank
// voices (Program Change 0-31, Bank1-1..8/Bank2-1..8/Bank3-1..8/Bank4-1..8,
// confirmed against the official Yamaha Data List), parsed byte-exact from
// the official .syx factory dumps shipped with the ESP32 reference project
// this codebase's FM engine was ported from
// (tools/refacedx/RDX-Reface-DX-emu/RDX/data/patches/*.syx) via a one-time
// host tool (see src/presets.cpp for provenance). Array order is deliberate
// and matches the real Program Change mapping exactly -- do not reorder.
#pragma once
#include "dx_engine/RDX_Types.h"

#define DX_NPRESETS 32

struct DxPreset {
    const char* name;
    RDX_Patch patch;
};

extern const DxPreset dxPresets[DX_NPRESETS];

class DX_Synth_Bridge;

void preset_apply(DX_Synth_Bridge* dx);   // producer only: copies the staged patch (dx_patch_stage()) into dx->patch()
void preset_stage(uint8_t idx);            // control side: stages dxPresets[idx].patch and sends IPC_CMD_DX_PATCH_APPLY
void preset_set_current(uint8_t idx);      // UI-side tracking
uint8_t preset_get_current(void);
