// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// kit.cpp - implementation of the shared panel look (module ui_kit).
//
// Drawn through u8g2 directly rather than through the Display facade: the
// header bar and the list cursor need an inverted draw colour, which the facade
// deliberately does not expose. Same arrangement as core/src/ui/list_view.cpp.

#include "picoface/ui_kit.h"

#include "u8g2.h"

namespace picoface {
namespace ui {
namespace kit {

namespace {

// ---------------------------------------------------------------------------
// The type scale, in one place
// ---------------------------------------------------------------------------
// Before the kit these were named at 66 call sites across the instruments,
// which is how PicoFaceD5 ended up on 5x7 while everyone else was on 6x10.
// A value is set in the largest face that leaves room for its name, because
// the value is what is read while playing; the name only has to be
// recognisable.
const uint8_t* const kFontTitle = u8g2_font_5x8_tf;
const uint8_t* const kFontLabel = u8g2_font_5x8_tf;
const uint8_t* const kFontValue = u8g2_font_profont15_tf;
const uint8_t* const kFontList  = u8g2_font_6x10_tf;
const uint8_t* const kFontSmall = u8g2_font_4x6_tf;

constexpr int16_t kW = Display::kWidth;   // 128

// ---------------------------------------------------------------------------
// Knob pointer, 270 degrees from 7:30 to 4:30, as a table
// ---------------------------------------------------------------------------
// A table rather than sinf/cosf, for two reasons that matter more than the
// handful of cycles saved: it pulls libm out of the UI path entirely, and it
// makes the firmware and the host renderer produce bit-identical pixels, which
// is what lets tools/host_tests/ui compare screens against stored images.
// 65 steps is finer than the ~42 distinguishable pixel positions on the rim of
// the largest knob the panel uses.
static const int8_t kPointerX[65] = {
     -90,  -96, -102, -107, -112, -116, -120, -122, -125, -126, -127, -127, -126,
    -125, -123, -121, -117, -113, -109, -104,  -98,  -92,  -85,  -78,  -71,  -63,
     -54,  -46,  -37,  -28,  -19,   -9,    0,    9,   19,   28,   37,   46,   54,
      63,   71,   78,   85,   92,   98,  104,  109,  113,  117,  121,  123,  125,
     126,  127,  127,  126,  125,  122,  120,  116,  112,  107,  102,   96,   90
};

static const int8_t kPointerY[65] = {
      90,   83,   76,   68,   60,   51,   43,   34,   25,   16,    6,   -3,  -12,
     -22,  -31,  -40,  -49,  -57,  -65,  -73,  -81,  -88,  -94, -100, -106, -111,
    -115, -118, -122, -124, -126, -127, -127, -127, -126, -124, -122, -118, -115,
    -111, -106, -100,  -94,  -88,  -81,  -73,  -65,  -57,  -49,  -40,  -31,  -22,
     -12,   -3,    6,   16,   25,   34,   43,   51,   60,   68,   76,   83,   90
};

inline int pointerIndex(float norm)
{
    if (norm <= 0.0f) return 0;
    if (norm >= 1.0f) return 64;
    return static_cast<int>(norm * 64.0f + 0.5f);
}

// Restores what the rest of the code assumes on entry, so a body drawn by an
// instrument after a kit call does not inherit an inverted colour.
inline void resetState(u8g2_t* u)
{
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFontPosBaseline(u);
}

} // namespace

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

void header(Display& d, const char* title, int page, int pageCount)
{
    u8g2_t* u = d.raw();

    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawBox(u, 0, 0, kW, kHeaderHeight);

    u8g2_SetDrawColor(u, 0);
    u8g2_SetFont(u, kFontTitle);
    if (title != nullptr && title[0] != 0) {
        u8g2_DrawStr(u, 2, 7, title);
    }

    // Dots, right aligned. Above eight pages the dots stop being countable, so
    // they become a coarse position instead of one dot per page - the point is
    // "roughly where am I", not an exact count.
    if (pageCount > 1) {
        const int n   = (pageCount > 8) ? 8 : pageCount;
        const int cur = (pageCount > 8) ? (page * 8) / pageCount : page;
        int16_t x = static_cast<int16_t>(kW - 3 - n * 5);
        for (int i = 0; i < n; ++i, x = static_cast<int16_t>(x + 5)) {
            if (i == cur) {
                u8g2_DrawDisc(u, static_cast<u8g2_uint_t>(x + 1), 4, 2, U8G2_DRAW_ALL);
            } else {
                u8g2_DrawCircle(u, static_cast<u8g2_uint_t>(x + 1), 4, 2, U8G2_DRAW_ALL);
            }
        }
    }

    resetState(u);
}

void footer(Display& d, const char* text)
{
    if (text == nullptr || text[0] == 0) return;

    u8g2_t* u = d.raw();
    resetState(u);
    u8g2_SetFont(u, kFontSmall);
    u8g2_DrawStr(u, 2, 63, text);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

namespace {

void drawKnob(u8g2_t* u, int16_t cx, int16_t cy, int16_t r, float norm)
{
    const int i = pointerIndex(norm);

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawCircle(u, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy),
                    static_cast<u8g2_uint_t>(r), U8G2_DRAW_ALL);

    // The pointer starts short of the centre: a line through the middle reads
    // as a diameter, not as a direction.
    //
    // End ticks outside the rim were tried and dropped: at r=9 they sit close
    // enough to the circle to read as noise, and the two ends are already told
    // apart by the value printed next to the knob. The knob is the glance, the
    // number is the detail.
    const int inner = 2;
    const int outer = r - 1;
    u8g2_DrawLine(u,
        static_cast<u8g2_uint_t>(cx + (kPointerX[i] * inner) / 127),
        static_cast<u8g2_uint_t>(cy + (kPointerY[i] * inner) / 127),
        static_cast<u8g2_uint_t>(cx + (kPointerX[i] * outer) / 127),
        static_cast<u8g2_uint_t>(cy + (kPointerY[i] * outer) / 127));
}

} // namespace

void knob(Display& d, int16_t cx, int16_t cy, int16_t r, float norm)
{
    drawKnob(d.raw(), cx, cy, r, norm);
}

void bar(Display& d, int16_t x, int16_t y, int16_t w, int16_t h, float norm)
{
    u8g2_t* u = d.raw();
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawFrame(u, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y),
                   static_cast<u8g2_uint_t>(w), static_cast<u8g2_uint_t>(h));
    const int16_t fill = static_cast<int16_t>((w - 2) * norm + 0.5f);
    if (fill > 0) {
        u8g2_DrawBox(u, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(y + 1),
                     static_cast<u8g2_uint_t>(fill), static_cast<u8g2_uint_t>(h - 2));
    }
}

void bigValue(Display& d, int16_t baselineY, const char* text)
{
    if (text == nullptr) return;

    u8g2_t* u = d.raw();
    resetState(u);
    u8g2_SetFont(u, kFontValue);
    const int16_t w = static_cast<int16_t>(u8g2_GetStrWidth(u, text));
    u8g2_DrawStr(u, static_cast<u8g2_uint_t>((kW - w) / 2),
                 static_cast<u8g2_uint_t>(baselineY), text);
}

// ---------------------------------------------------------------------------
// Body: two values, one per parameter encoder
// ---------------------------------------------------------------------------

namespace {

// One half of the duo layout. 'x' is the left edge of the 64 px column.
//
// The name gets a line of its own across the full width of the cell rather
// than sitting beside the knob. Beside it there was room for seven characters,
// and the Model D alone has "Emphasis", "Contour" and "Keyboard" - a layout
// that truncates the label of a control is worse than one that spends a row on
// it.
void duoCell(u8g2_t* u, int16_t x, const Param& p, char encoder)
{
    constexpr int16_t kNameBase  = 20;   // baseline of the name row
    constexpr int16_t kKnobCy    = 37;   // centre of the knob
    constexpr int16_t kKnobR     = 9;
    constexpr int16_t kValueBase = 43;   // baseline of the value

    u8g2_SetDrawColor(u, 1);

    if (p.name == nullptr || p.name[0] == 0) {
        if (p.text == nullptr || p.text[0] == 0) {
            // An empty slot is drawn as empty rather than skipped: a blank
            // half says "this encoder does nothing here", a missing one does
            // not.
            u8g2_SetFont(u, kFontValue);
            u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 26), kValueBase, "--");
            return;
        }
        // A value that names itself (a preset) gets the whole cell.
        u8g2_SetFont(u, kFontValue);
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 4), kValueBase, p.text);
    } else {
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 4), kNameBase, p.name);

        int16_t valueX;
        if (p.norm < 0.0f) {
            // No position to show: a marker that says the value steps through
            // a list instead of sweeping.
            u8g2_DrawTriangle(u, static_cast<u8g2_uint_t>(x + 5),  static_cast<u8g2_uint_t>(kKnobCy - 5),
                                 static_cast<u8g2_uint_t>(x + 10), static_cast<u8g2_uint_t>(kKnobCy),
                                 static_cast<u8g2_uint_t>(x + 5),  static_cast<u8g2_uint_t>(kKnobCy + 5));
            valueX = static_cast<int16_t>(x + 14);
        } else {
            drawKnob(u, static_cast<int16_t>(x + 13), kKnobCy, kKnobR, p.norm);
            valueX = static_cast<int16_t>(x + 26);
        }

        // The value takes the large face where it fits and the label face
        // where it does not, rather than running into the next cell. Long
        // values are the exception ("Pink noise", "Filt Env"), so the panel
        // does not look mixed in normal use.
        const char* text = (p.text != nullptr) ? p.text : "";
        u8g2_SetFont(u, kFontValue);
        if (u8g2_GetStrWidth(u, text) > (x + 62 - valueX)) {
            u8g2_SetFont(u, kFontLabel);
        }
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(valueX), kValueBase, text);
    }

    // Which encoder owns this half. Small on purpose: it is a reminder for the
    // first minutes with an instrument, not something to read every time.
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, kFontSmall);
    const char label[2] = { encoder, 0 };
    u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 2), 53, label);
}

} // namespace

void panelDuo(Display& d, const Param& a, const Param& b)
{
    u8g2_t* u = d.raw();
    resetState(u);

    duoCell(u, 0,  a, 'A');
    duoCell(u, 64, b, 'B');

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawVLine(u, 63, kBodyTop + 2, 42);
}

// ---------------------------------------------------------------------------
// Body: a name across the full width, one parameter below
// ---------------------------------------------------------------------------

void panelName(Display& d, const char* text, const char* sub, const Param& b)
{
    u8g2_t* u = d.raw();
    resetState(u);

    // The name, in the largest face it fits into. A patch name is what the
    // player is looking for on this page, so it gets the width and the size
    // before anything else on it does.
    if (text != nullptr && text[0] != 0) {
        u8g2_SetFont(u, kFontValue);
        if (u8g2_GetStrWidth(u, text) > kW - 8) {
            u8g2_SetFont(u, kFontList);
            if (u8g2_GetStrWidth(u, text) > kW - 8) {
                u8g2_SetFont(u, kFontLabel);
            }
        }
        u8g2_DrawStr(u, 4, 27, text);
    }

    if (sub != nullptr && sub[0] != 0) {
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, 4, 37, sub);
    }

    u8g2_DrawHLine(u, 0, 40, kW);

    // The right-hand encoder's parameter, laid out along the line rather than
    // stacked: there is one of them and a whole width to put it in.
    if (b.name != nullptr && b.name[0] != 0) {
        int16_t nameX = 4;
        if (b.norm >= 0.0f) {
            drawKnob(u, 13, 50, 7, b.norm);
            nameX = 26;
        }
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(nameX), 53, b.name);

        const char* bt = (b.text != nullptr) ? b.text : "";
        u8g2_SetFont(u, kFontValue);
        const int16_t tw = static_cast<int16_t>(u8g2_GetStrWidth(u, bt));
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(kW - 10 - tw), 53, bt);

        u8g2_SetFont(u, kFontSmall);
        u8g2_DrawStr(u, kW - 6, 53, "B");
    }

    resetState(u);
}

// ---------------------------------------------------------------------------
// Body: two-column browser
// ---------------------------------------------------------------------------

namespace {

void listColumn(u8g2_t* u, int16_t x, int16_t w, const char* const* entries,
                int count, int sel, bool focused)
{
    constexpr int kRows   = 4;
    constexpr int kPitch  = 11;

    u8g2_SetFont(u, kFontList);

    // Keep the cursor one row from the top where there is room below, so the
    // list shows where it is going rather than only where it has been.
    int top = sel - 1;
    if (top > count - kRows) top = count - kRows;
    if (top < 0)             top = 0;

    for (int i = 0; i < kRows && top + i < count; ++i) {
        const int idx = top + i;
        const int16_t y = static_cast<int16_t>(kBodyTop + i * kPitch);

        if (idx == sel) {
            if (focused) {
                u8g2_SetDrawColor(u, 1);
                u8g2_DrawBox(u, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y),
                             static_cast<u8g2_uint_t>(w), kPitch);
                u8g2_SetDrawColor(u, 0);
            } else {
                // The unfocused column still marks its position, but as an
                // outline: two solid bars would leave the eye no idea which
                // one the encoder is actually moving.
                u8g2_SetDrawColor(u, 1);
                u8g2_DrawFrame(u, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y),
                               static_cast<u8g2_uint_t>(w), kPitch);
            }
        }

        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 3), static_cast<u8g2_uint_t>(y + 9),
                     entries[idx] ? entries[idx] : "");
        u8g2_SetDrawColor(u, 1);
    }
}

} // namespace

void list(Display& d, const char* const* entries, int count, int sel)
{
    u8g2_t* u = d.raw();
    resetState(u);
    if (entries != nullptr) {
        listColumn(u, 0, kW, entries, count, sel, true);
    }
    resetState(u);
}

void listTwoCol(Display& d,
                const char* const* left, int leftCount, int leftSel,
                const char* const* right, int rightCount, int rightSel,
                bool focusRight)
{
    u8g2_t* u = d.raw();
    resetState(u);

    constexpr int16_t kSplit = 48;
    if (left != nullptr) {
        listColumn(u, 0, kSplit - 1, left, leftCount, leftSel, !focusRight);
    }
    u8g2_SetDrawColor(u, 1);
    u8g2_DrawVLine(u, kSplit, kHeaderHeight, 64 - kHeaderHeight);
    if (right != nullptr) {
        listColumn(u, kSplit + 2, kW - kSplit - 2, right, rightCount, rightSel, focusRight);
    }
    resetState(u);
}

// ---------------------------------------------------------------------------
// About
// ---------------------------------------------------------------------------

void about(Display& d, const char* name, const char* version, const char* hint)
{
    u8g2_t* u = d.raw();

    d.clear();
    header(d, "ABOUT");
    resetState(u);

    if (name != nullptr) {
        u8g2_SetFont(u, kFontValue);
        u8g2_DrawStr(u, 4, 27, name);
    }
    if (version != nullptr) {
        // Smaller than the name above it: the version is a git describe, and
        // "1.7.0-4-ga39504b" needs seventeen characters. A clipped commit hash
        // still reads like a whole one, but only if it is not clipped mid-word.
        u8g2_SetFont(u, kFontList);
        u8g2_DrawStr(u, 4, 40, version);
    }
    if (hint != nullptr) {
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, 4, 53, hint);
    }
    resetState(u);
}

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

void popup(Display& d, const char* title, const char* text)
{
    u8g2_t* u = d.raw();
    constexpr int16_t x = 8, y = 18, w = 112, h = 30;

    // Cleared first: a popup over a panel has to be opaque or neither is
    // readable.
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, x, y, w, h);

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawFrame(u, x, y, w, h);
    u8g2_DrawBox(u, x, y, w, 9);

    u8g2_SetDrawColor(u, 0);
    u8g2_SetFont(u, kFontTitle);
    if (title != nullptr) u8g2_DrawStr(u, x + 3, y + 7, title);

    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, kFontValue);
    if (text != nullptr) {
        const int16_t tw = static_cast<int16_t>(u8g2_GetStrWidth(u, text));
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + (w - tw) / 2),
                     static_cast<u8g2_uint_t>(y + 25), text);
    }
    resetState(u);
}

} // namespace kit
} // namespace ui
} // namespace picoface
