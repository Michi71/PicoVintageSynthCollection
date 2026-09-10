// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// -----------------------------------------------------------------------------
// picoface/ui_kit.h
//
// The shared look of every PicoFace front panel, part of the optional core
// module "ui_kit".
//
// Before this module each instrument drew its own header bar, its own two body
// lines and its own footer -- the same thirty lines of u8g2 calls copied into
// eight instruments, which is why the fonts had already started to drift apart
// between them. An instrument now states WHAT is on the page and the kit
// decides how it looks, so a change to the house style is one edit rather than
// ten.
//
// What the kit deliberately does NOT do: navigation. Which page is shown, what
// an encoder does and when to redraw stays with the instrument -- those differ
// for good reasons (a Juno browses patches, a D-50 browses two tones) and
// unifying them would cost behaviour, not duplication.
//
// Nothing here sends the buffer. The caller draws and then arms the staged
// flush through Display::flush(), exactly as before.
// -----------------------------------------------------------------------------

#ifndef PICOFACE_UI_KIT_H
#define PICOFACE_UI_KIT_H

#include <cstdint>

#include "picoface/ui.h"

namespace picoface {
namespace ui {
namespace kit {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
// Published so that an instrument drawing its own body (the reface DX
// algorithm diagram, a scope, a level meter) knows the box it may use without
// guessing: everything from kBodyTop to kBodyBottom belongs to it.
constexpr int16_t kHeaderHeight = 12;
constexpr int16_t kBodyTop      = kHeaderHeight + 1;   // 13
constexpr int16_t kBodyBottom   = 63;                  // the screen's last row
constexpr int16_t kBodyHeight   = kBodyBottom - kBodyTop + 1;

// ---------------------------------------------------------------------------
// One value on the panel, as the instrument states it
// ---------------------------------------------------------------------------
struct Param {
    // Short name, as printed on the original panel: "Cutoff", "Reso".
    const char* name = "";

    // The value, already formatted by the instrument -- it owns the units and
    // the rounding ("62", "16'", "Saw", "-12 st", "Omni"). The kit never
    // formats a number, because only the instrument knows what the number is.
    const char* text = "";

    // 0..1 for anything with a position: it drives the knob pointer, which is
    // what makes a value readable across the room without reading the digits.
    // Leave it negative for a value that has no position -- an enum, a patch
    // name, a MIDI channel -- and the kit draws a list marker instead.
    float norm = -1.0f;

    // Set for a value the instrument considers switched off / neutral, so the
    // kit can grey it without the instrument drawing anything itself.
    bool dimmed = false;
};

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

// Inverted title bar. 'page' is 0-based; pass pageCount <= 1 to omit the dots.
// The dots replace the old "3/7" because at a glance a filled dot among empty
// ones reads as position, while two digits have to be parsed.
void header(Display& d, const char* title, int page = 0, int pageCount = 0);

// There is no footer. There was one - CPU load, underruns, dropped packets -
// and it cost the body nine rows on a screen that had none to spare (#161: the
// panel was hard to read from where a synth actually sits). Those numbers are
// for whoever is developing the engine, not for whoever is playing it, so they
// went onto a screen of their own: see diagnostics() below.

// ---------------------------------------------------------------------------
// Body layouts
// ---------------------------------------------------------------------------

// Two values side by side, one per parameter encoder -- the layout every
// instrument already uses, with the knobs added. 'a' belongs to Encoder::ParamA
// on the left, 'b' to Encoder::ParamB on the right.
void panelDuo(Display& d, const Param& a, const Param& b);

// A page whose main value is a name rather than a number: a patch, a preset,
// an instrument. The name gets the full width - "1-12 Nightfall" does not fit
// in half of it at any legible size - with an optional second line under it
// for what the name belongs to (a bank, a structure), and one parameter for
// the right-hand encoder below that.
void panelName(Display& d, const char* text, const char* sub, const Param& b);

// Single column, full width: a section menu or a preset list. Four rows fit
// between the header and the footer, one more than the old three-row list, and
// the cursor sits one row from the top so the list shows where it is going.
void list(Display& d, const char* const* entries, int count, int sel);

// Two-column browser: sections on the left, entries of the selected section on
// the right. Replaces the single-column ListView wherever a menu has a level
// above it, which is every instrument's section menu.
void listTwoCol(Display& d,
                const char* const* left, int leftCount, int leftSel,
                const char* const* right, int rightCount, int rightSel,
                bool focusRight);

// The developer's screen: up to four rows of whatever the engine wants to
// report - CPU peak, underruns, dropped packets, voices. Each instrument
// formats its own rows because only it knows what its numbers are; the kit
// only puts them where every instrument puts them.
void diagnostics(Display& d, const char* title, const char* const* rows, int count);

// The About screen every instrument has: what this is and which build. The
// hint line is what the instrument wants to say underneath - "Press any
// button", or a diagnostic when something is worth reporting.
void about(Display& d, const char* name, const char* version, const char* hint);

// Transient overlay over whatever is already drawn: a patch change, a stored
// slot, a warning. The caller decides how long it stays up.
void popup(Display& d, const char* title, const char* text);

// ---------------------------------------------------------------------------
// Primitives, for instruments that draw their own body
// ---------------------------------------------------------------------------

// A pointer knob sweeping 270 degrees, 7:30 to 4:30, so both ends are
// unmistakable. norm outside 0..1 is clamped.
void knob(Display& d, int16_t cx, int16_t cy, int16_t r, float norm);

// Horizontal fill gauge. Cheaper than a knob where many values sit in a row.
void bar(Display& d, int16_t x, int16_t y, int16_t w, int16_t h, float norm);

// Centred single line in the value font -- the "one big thing" screen (a patch
// name, a stored slot number).
void bigValue(Display& d, int16_t baselineY, const char* text);

} // namespace kit
} // namespace ui
} // namespace picoface

#endif // PICOFACE_UI_KIT_H
