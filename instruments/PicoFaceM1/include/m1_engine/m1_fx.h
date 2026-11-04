// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_fx.h -- the M1's effect section: two effects out of 33 types, serial or
// parallel, DRY:EFF balance per effect half and the Out-3/4 panpots.
//
// The MDE effect chip is a programmable DSP whose instruction semantics are not
// fully decoded (doc/ENGINE.md, "Effects"), so this is an ALGORITHMIC port: one
// class per firmware type, structure as worked out in the analysis, every
// coefficient and delay length from the firmware's own tables in the user's OS
// ROM (FxTables, generated at configure time).
//
// Memory follows the MDE: 64 K words of delay RAM, effect 1 in one half,
// effect 2 in the other, each half one ring with a pointer that runs DOWN once
// per sample. Every delay line is a fixed offset range in its half, so the
// layout is the firmware's in spirit and the 128 KB budget is the chip's.
#ifndef M1_FX_H
#define M1_FX_H

#include <stdint.h>

namespace m1 {

// ---------------------------------------------------------------------------
// Tables read from the OS ROM by tools/m1_extract (m1_fx_tables.py). Field
// names and meaning match the Python reference m1_effects.py.
// ---------------------------------------------------------------------------
struct FxRevType {          // Hall, Ensemble Hall, Concert Hall, Room, Large Room, Live Stage
    float   g[4];           // allpass gains AP0..AP3
    float   comb_out[8];    // comb tap gains L0,R0,L1,R1,...
    int16_t ap_d[4], comb_d[4], tap_l[4], tap_r[4];
    float   g_in;
    float   fb[4][98];      // comb feedback per Reverb Time byte (signed as used)
    float   damp[4][100];   // comb damping per High Damp byte
    int16_t base_ap, base_erl, base_err, base_erc;
    float   er_scale[3];    // centre, left, right early-reflection scale
};

struct FxErType { float g[4]; float tg[25]; };

struct FxRotary {
    float   c20, c24, c28, c2c, c30, c38, c40, c44;
    int16_t h_fb_tap, h_w, h_fix, h_base, d_w, d_base;
    int16_t micL[5], micR[5];
    float   g120, g124, g130, g134;
    int16_t accel[2];
    int16_t slow[21], fast[21];
    float   depth_k[4];
};

struct FxSym { float mix2[3]; int16_t baseL[3], baseR[3]; int16_t inc[2]; int16_t off[3]; };

struct FxComboRev {         // second half of Delay/Hall (25) and Delay/Room (26)
    float   g[3], ger[2], cout[4];
    int16_t ap_d[3], comb_d[4], comb_tap[4];
    float   eq[6];          // Z1, P1, Z2, P2, s1, s2 of the two fixed shelves
    float   g_in;
    float   fb[4][98];
    float   damp[4][100];
    int16_t base_ap, base_b, base_c;
};

struct FxComboEr { float g[4], tg[25], T[25], A[4]; float g_in; };

struct FxTables {
    FxRevType  rev[6];
    float      dly_a[100], dly_b[100];     // delay-loop damping (a = pole, b = gain)
    float      wt[0x102 + 130];            // LFO: sine at 0, triangle at 0x102
    float      wt2[130];                   // rectified sine, double frequency
    int32_t    lfo_inc[217];               // Speed byte -> 20-bit phase increment
    FxErType   er[3];                      // Early Ref 1..3
    float      er_T[25], er_A[4];
    float      trem_k, exc_k;
    FxRotary   rot;
    FxSym      sym;
    FxComboRev crev[2];
    FxComboEr  cer;
    int16_t    eq_k[5];                    // DS:8966 Q13: shelf corners 250/500 Hz, 1/2/4 kHz
    int16_t    eq_gain[13];                // DS:8970 Q15: 0..-12 dB in 1 dB steps
};

// The effect block as stored in a program (bytes 38..62) or combination
// (bytes 11..35): types, balances, panpots, switches, 2 x 8 parameter bytes.
enum { kFxBlockLen = 25, kFxNoEffect = 33, kFxTypes = 34 };

class FxUnit;   // one effect, defined in m1_fx.cpp

class EffectSection {
public:
    // tables: from the ROM. mem: 65536 int16 words of RAM (128 KB) for the
    // delay lines; the section never allocates.
    void init(const FxTables* tables, const uint16_t* panTable101,
              int16_t* mem);

    // Rebuilds both effects from a 25-byte effect block. Clears the delay RAM.
    void configure(const uint8_t* block);

    // a/b/c/d: the four voice buses. Writes outputs 1/2 (the stereo pair the
    // instrument plays). Out 3/4 are not wired on the PicoFace and are dropped.
    void process(const float* a, const float* b, const float* c, const float* d,
                 float* out1, float* out2, int n);

    // Rotary speaker slow/fast, the M1's pedal/MIDI switch. Ignored otherwise.
    void setRotaryFast(bool fast);

    int type(int k) const { return type_[k]; }
    // True if an effect's delay lines did not fit its 32 K words (a bug).
    bool overflow() const;

private:
    void runUnit(int k, float l, float r, float& ol, float& orr);

    const FxTables* t_ = nullptr;
    const uint16_t* pan_ = nullptr;   // DS:86EA, 101 entries Q9
    int16_t* mem_ = nullptr;
    FxUnit* fx_[2] = {nullptr, nullptr};
    int type_[2] = {kFxNoEffect, kFxNoEffect};
    bool on_[2] = {false, false};
    bool serial_ = false;
    // DRY:EFF gains per effect half (1L, 1R, 2L, 2R). The firmware ramps a
    // change in over 32 ticks (F298:017F); balances only move on a program
    // change here, so they are simply set.
    float dry_[4] = {1, 1, 1, 1}, wet_[4] = {0, 0, 0, 0};
    float p3_[2] = {0, 0}, p4_[2] = {0, 0};    // panpot (L, R) gains
    alignas(8) uint8_t store_[2][1024];          // placement storage for FxUnit
};

} // namespace m1

#endif // M1_FX_H
