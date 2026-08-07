// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// Stand-in for the engine. midi_reface.cpp only ever reaches through it for
// patch(), so the FM engine and everything under it stays out of this build.
#pragma once
#include "dx_engine/RDX_Types.h"

class DX_Synth_Bridge {
public:
    RDX_Patch&       patch()       { return p_; }
    const RDX_Patch& patch() const { return p_; }
private:
    RDX_Patch p_{};
};
