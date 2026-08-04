/*
  presets.h -- Static factory presets replacing the mda program layer.

  Deviation from the original Yamaha Reface CP (which has no presets):
  each preset bundles instrument + engine params + FX-chain settings and
  is selected via CC80 zones (8 equal zones) or the PRESETS menu.
*/
#pragma once
#include <stdint.h>

class mdaEPiano;
class RefaceCpChain;

#define CP_NPRESETS 8

struct CpPreset {
    const char* name;        // <= 15 chars, shown on 128px OLED
    uint8_t instrument;      // 0..5 = Rd I, Rd II, Wr, Clv, Pno, CP
    float engine[12];        // mdaEPiano params 0..11
    uint8_t twMode;          // 0 Off, 1 Tremolo, 2 Wah
    uint8_t cpMode;          // 0 Off, 1 Chorus, 2 Phaser
    uint8_t dlyMode;         // 0 Off, 1 Digital, 2 Analog
    float drive, twDepth, twRate, cpDepth, cpSpeed, dlyDepth, dlyTime, reverb; // all 0..1
};

extern const CpPreset cpPresets[CP_NPRESETS];

void preset_apply(uint8_t idx, mdaEPiano* ep, RefaceCpChain* fx); // Core 0 only (called from IPC dispatch)
void preset_set_current(uint8_t idx); // Core-1-side UI tracking
uint8_t preset_get_current(void);
