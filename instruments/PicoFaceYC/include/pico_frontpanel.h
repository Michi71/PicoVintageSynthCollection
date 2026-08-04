// pico_frontpanel.h
//
// Three-encoder paged "virtual front panel" for the Reface DX
// (RP2350 + SH1106 128x64 OLED).
//
//   - The SELECTOR encoder pages through the DX pages (OP1-4/LFO/ALGO).
//   - The PARAM A and PARAM B encoders edit the two values of the current page.
//   - A LONG press (>= 500 ms) of the selector opens the Presets / System main menu.

#pragma once

#include "u8g2.h"
#include "encoder.h"
#include "push_button.h"

// Home screen. Never returns (loops forever as the instrument UI).
void pico_UserInterfaceFrontPanel(u8g2_t* u8g2,
                                  Encoder* encSel, PushButton* btSel,
                                  Encoder* encA, PushButton* btA,
                                  Encoder* encB, PushButton* btB);
