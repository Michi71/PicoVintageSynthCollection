// pico_frontpanel.h
//
// Three-encoder paged "virtual front panel" for the Reface CP
// (RP2350 + SH1106 128x64 OLED).
//
//   - The SELECTOR encoder pages through the UI screens (one parameter set
//     per screen).  Each screen exposes up to two values.
//   - A SHORT press of the selector cycles the effect mode on the
//     Tremolo/Wah, Chorus/Phaser and D.Delay/A.Delay screens (Off -> A -> B).
//   - A LONG press of the selector opens the Presets / System main menu.
//   - The PARAM A and PARAM B encoders set the two on-screen values.
//   - The optional Param A / Param B switches reset their value to a default.

#pragma once

#include "u8g2.h"
#include "encoder.h"
#include "push_button.h"
#include "mdaEPiano.h"
#include "reface_cp_chain.h"

// Home screen.  Never returns (loops forever as the instrument UI).
void pico_UserInterfaceFrontPanel(u8g2_t* u8g2,
                                  Encoder* encSel, PushButton* btSel,
                                  Encoder* encA, PushButton* btA,
                                  Encoder* encB, PushButton* btB,
                                  mdaEPiano* ep, RefaceCpChain* fx);
