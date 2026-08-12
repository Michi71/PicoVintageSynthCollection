// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// What the common block hands a partial each sample. The LFOs and the pitch
// envelope live in the tone, the partials only say how far they follow them,
// so the modulation arrives here already summed.
#pragma once

namespace d5 {

// The control rate: LFOs, pitch envelope and routing run every kModPeriod
// samples -- 2 kHz at the engine's 32 kHz -- with pitch and amplitude
// ramped linearly across the block. Nothing routed here moves faster than
// 28 Hz, and computing it per sample was most of what a voice cost.
inline constexpr int kModPeriod = 16;

struct Modulation {
    float pitch = 1.0f;    // multiplies the playback rate / frequency
    float amp = 1.0f;      // multiplies the output level (TVA modulation)
    float cutoff = 0.0f;   // added to the 0..1 cutoff (TVF modulation)
    float pw = 0.0f;       // added to the pulse width
};

}  // namespace d5
