// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#ifndef DX_CONTROLLER_H
#define DX_CONTROLLER_H

#include "DX_Synth_Bridge.h"

// ---------------------------------------------------------------------------
// Menu page enumeration. One page per logical control context.
// Order matters: OP1..OP4 index directly into patch.ops[page].
// FX1/FX2 = the reface DX's 2 effect slots (Type + Param1 editable from the
// front panel; Param2 is SysEx-only, matching how many other patch parameters
// -- e.g. voice name, KSC curves -- are already SysEx-only and not reachable
// from the 3-encoder front panel).
// ---------------------------------------------------------------------------
enum class DxPage : uint8_t { OP1 = 0, OP2, OP3, OP4, LFO, ALGO, FX1, FX2, COUNT };

// Architektur-Hinweis:
// Die Klasse laeuft im UI-Kontext (uiTick auf core0). Parameteraenderungen durch
// Encoder2/3 gehen ueber den Ring (ipc_send_dx_param) an den Audioproduzenten,
// der den Patch zu Blockbeginn mutiert - nie direkt aus dem Encoder-Pfad.
// MIDI-Notenweiterleitung an die DX-Engine passiert nicht hier, sondern direkt
// in RefaceMidi (inkl. Kanal-Filter/Transpose/Soft-Pedal-Logik).
class DX_Controller {
public:
    explicit DX_Controller(DX_Synth_Bridge& bridge)
        : bridge_(bridge), page_(DxPage::OP1) {}

    // --- Encoder handlers (implemented in .cpp) ----------------------------
    void onEncoder1(int delta);
    void onEncoder2(int delta);
    void onEncoder3(int delta);

    // --- Accessors ---------------------------------------------------------
    DxPage currentPage() const { return page_; }
    RDX_Patch& patch() const { return bridge_.patch(); }
    const char* pageName() const;

private:
    DX_Synth_Bridge& bridge_;
    DxPage          page_;

    // Helper: advance page index with bidirectional wraparound.
    static DxPage advancePage(DxPage current, int delta);
};

#endif // DX_CONTROLLER_H
