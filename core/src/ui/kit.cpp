// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// kit.cpp - implementation of the shared panel look (module ui_kit).
//
// Drawn through u8g2 directly rather than through the Display facade: the
// header bar and the list cursor need an inverted draw colour, which the facade
// deliberately does not expose. Same arrangement as core/src/ui/list_view.cpp.

#include "picoface/ui_kit.h"

#include <cstring>

#include "u8g2.h"

namespace picoface {
namespace ui {
namespace kit {

namespace {

// ---------------------------------------------------------------------------
// The type scale, in one place
// ---------------------------------------------------------------------------
// Bold throughout, and larger than the first version of this kit. That one
// used 5x8 for names and a light 15 px face for values, and the first report
// from a user (#161) was that it read worse than the panels it replaced: a
// synth sits off to the side or behind a keyboard, and thin strokes vanish at
// that distance. The previous panels were 8x13 bold; this is the same weight,
// with the values a size up.
//
// A value is set in the largest bold face that still leaves room for the knob
// beside it. helvB14 is that size: at 18 px "-1.6" no longer fits next to the
// knob and would fall back to the label face, which is worse than 14 px
// everywhere.
const uint8_t* const kFontTitle = u8g2_font_7x13B_tf;
const uint8_t* const kFontLabel = u8g2_font_6x12_tf;
const uint8_t* const kFontValue = u8g2_font_helvB14_tf;
// One size under the value face, for the patch name on the name page. Not a
// concession: the name's band is two tile rows, 16..31, and helvB14 with its
// ascenders and descenders is 18 rows tall - at any baseline two of them fall
// outside the band and the marquee clips them off ("Nighttall"). helvB12 is
// 16 rows exactly, measured on the host by scanning the buffer, and a name
// in it fits the width beside the number more often, so it scrolls less.
const uint8_t* const kFontName  = u8g2_font_helvB12_tf;
const uint8_t* const kFontList  = u8g2_font_7x13B_tf;
const uint8_t* const kFontSmall = u8g2_font_4x6_tf;

constexpr int16_t kW = Display::kWidth;   // 128

// ---------------------------------------------------------------------------
// Knob pointer, 270 degrees from 7:30 to 4:30, as a table
// ---------------------------------------------------------------------------
// A table rather than sinf/cosf, for two reasons that matter more than the
// handful of cycles saved: it pulls libm out of the UI path entirely, and it
// makes the firmware and the host renderer produce bit-identical pixels, which
// is what lets tools/host_tests/ui compare screens against stored images.
// 65 steps is finer than the ~50 distinguishable pixel positions on the rim of
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

void drawKnob(u8g2_t* u, int16_t cx, int16_t cy, int16_t r, float norm)
{
    const int i = pointerIndex(norm);

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawCircle(u, static_cast<u8g2_uint_t>(cx), static_cast<u8g2_uint_t>(cy),
                    static_cast<u8g2_uint_t>(r), U8G2_DRAW_ALL);

    // The pointer starts short of the centre: a line through the middle reads
    // as a diameter, not as a direction.
    //
    // End ticks outside the rim were tried and dropped: they sit close enough
    // to the circle to read as noise, and the two ends are already told apart
    // by the value printed next to the knob. The knob is the glance, the
    // number is the detail.
    const int inner = 2;
    const int outer = r - 1;
    u8g2_DrawLine(u,
        static_cast<u8g2_uint_t>(cx + (kPointerX[i] * inner) / 127),
        static_cast<u8g2_uint_t>(cy + (kPointerY[i] * inner) / 127),
        static_cast<u8g2_uint_t>(cx + (kPointerX[i] * outer) / 127),
        static_cast<u8g2_uint_t>(cy + (kPointerY[i] * outer) / 127));
}

// Draws 'text' in the value face where it fits into 'width' pixels, and in
// the label face where it does not, rather than running into the next cell.
// Long values are the exception ("Vibrato", "Pink noise"), so the panel does
// not look mixed in normal use.
void drawValue(u8g2_t* u, int16_t x, int16_t baseline, int16_t width, const char* text)
{
    if (text == nullptr) text = "";
    u8g2_SetFont(u, kFontValue);
    if (static_cast<int16_t>(u8g2_GetStrWidth(u, text)) > width) {
        u8g2_SetFont(u, kFontLabel);
    }
    u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(baseline), text);
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
        u8g2_DrawStr(u, 2, 10, title);
    }

    // Dots, right aligned. Above eight pages the dots stop being countable, so
    // they become a coarse position instead of one dot per page - the point is
    // "roughly where am I", not an exact count.
    if (pageCount > 1) {
        const int n   = (pageCount > 8) ? 8 : pageCount;
        const int cur = (pageCount > 8) ? (page * 8) / pageCount : page;
        const int16_t cy = kHeaderHeight / 2;
        int16_t x = static_cast<int16_t>(kW - 3 - n * 5);
        for (int i = 0; i < n; ++i, x = static_cast<int16_t>(x + 5)) {
            if (i == cur) {
                u8g2_DrawDisc(u, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(cy), 2, U8G2_DRAW_ALL);
            } else {
                u8g2_DrawCircle(u, static_cast<u8g2_uint_t>(x + 1), static_cast<u8g2_uint_t>(cy), 2, U8G2_DRAW_ALL);
            }
        }
    }

    resetState(u);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

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
    // The value budget beside the knob is 36 px, measured against what the
    // instruments actually print in helvB14: "+0.4" is 36 (a plus is wider
    // than a minus), "0.35" 34, "-1.6" and "100" 30. One pixel less on the
    // knob bought the three that "+0.4" needed.
    constexpr int16_t kNameBase  = 25;   // baseline of the name row
    constexpr int16_t kKnobCx    = 14;   // centre of the knob, from the cell's left edge
    constexpr int16_t kKnobCy    = 45;
    constexpr int16_t kKnobR     = 10;
    constexpr int16_t kValueBase = 51;   // baseline of the value
    constexpr int16_t kValueX    = 27;   // left edge of the value beside a knob

    u8g2_SetDrawColor(u, 1);

    if (p.name == nullptr || p.name[0] == 0) {
        if (p.text == nullptr || p.text[0] == 0) {
            // An empty slot is drawn as empty rather than skipped: a blank
            // half says "this encoder does nothing here", a missing one does
            // not.
            u8g2_SetFont(u, kFontValue);
            u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + kValueX), kValueBase, "--");
            return;
        }
        // A value that names itself (a preset) gets the whole cell.
        drawValue(u, static_cast<int16_t>(x + 4), kValueBase, 58, p.text);
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
            valueX = static_cast<int16_t>(x + 13);   // 50 px: "White" and "Norm" stay bold
        } else {
            drawKnob(u, static_cast<int16_t>(x + kKnobCx), kKnobCy, kKnobR, p.norm);
            valueX = static_cast<int16_t>(x + kValueX);
        }
        // A string of width W drawn at valueX ends at valueX + W - 1, and the
        // cell's last usable column is x + 62.
        drawValue(u, valueX, kValueBase, static_cast<int16_t>(x + 63 - valueX), p.text);
    }

    // Which encoder owns this half. Small on purpose: it is a reminder for the
    // first minutes with an instrument, not something to read every time.
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, kFontSmall);
    const char label[2] = { encoder, 0 };
    u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 2), 62, label);
}

} // namespace

void panelDuo(Display& d, const Param& a, const Param& b)
{
    u8g2_t* u = d.raw();
    resetState(u);

    duoCell(u, 0,  a, 'A');
    duoCell(u, 64, b, 'B');

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawVLine(u, 63, kBodyTop + 2, static_cast<u8g2_uint_t>(kBodyHeight - 3));
}

// ---------------------------------------------------------------------------
// Body: a name across the full width, one parameter below
// ---------------------------------------------------------------------------

namespace {

// The one marquee the panel can show. Its state lives here rather than in the
// instruments because the animation is the kit's: an instrument only has to
// keep calling marqueeTick() while panelName() said the name scrolls.
struct Marquee {
    char     text[48];
    int16_t  x;         // left edge of the name region
    int16_t  width;     // region width, up to the right margin
    int16_t  textW;     // the name's width in the value face
    int16_t  lastOff;   // offset last drawn, so a frame that changes nothing is not pushed
    uint32_t startMs;   // when this name first appeared
    uint32_t seenMs;    // when panelName() last drew it
    bool     active;
};
Marquee g_marquee = {};

constexpr int16_t  kNameBase = 28;     // baseline of number and name, inside the band
constexpr uint32_t kHoldMs   = 1200;   // a new name stays put this long before it moves
constexpr int16_t  kSpeedPxS = 48;     // then it moves at this rate
constexpr int16_t  kGapPx    = 24;     // between the end of the name and its next copy

// A page that was away and comes back gets its hold again: panelName() is
// called at least every 500 ms while its page is up, so a longer gap means
// the page was somewhere else in between.
constexpr uint32_t kAwayMs = 800;

int16_t marqueeOffset(uint32_t nowMs)
{
    const uint32_t elapsed = nowMs - g_marquee.startMs;
    if (elapsed < kHoldMs) return 0;
    const uint32_t period = static_cast<uint32_t>(g_marquee.textW + kGapPx);
    // 64-bit: a patch page left up for a day would otherwise wrap the product.
    const uint64_t px = static_cast<uint64_t>(elapsed - kHoldMs) * static_cast<uint64_t>(kSpeedPxS) / 1000u;
    return static_cast<int16_t>(px % period);
}

// The name and its next copy one period to the right, both clipped to the
// region. u8g2 carries 16-bit signed intermediate coordinates in every 32-bit
// build (U8G2_16BIT), so a copy that starts left of the region is clipped
// rather than wrapped - checked on the host renderer before relying on it.
void drawMarqueeBand(u8g2_t* u, int16_t off)
{
    const int16_t period = static_cast<int16_t>(g_marquee.textW + kGapPx);
    u8g2_SetClipWindow(u, static_cast<u8g2_uint_t>(g_marquee.x), static_cast<u8g2_uint_t>(kNameBandTop),
                       static_cast<u8g2_uint_t>(g_marquee.x + g_marquee.width),
                       static_cast<u8g2_uint_t>(kNameBandBottom + 1));
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, kFontName);
    const int16_t x0 = static_cast<int16_t>(g_marquee.x - off);
    u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x0), static_cast<u8g2_uint_t>(kNameBase), g_marquee.text);
    u8g2_DrawStr(u, static_cast<u8g2_uint_t>(static_cast<int16_t>(x0 + period)),
                 static_cast<u8g2_uint_t>(kNameBase), g_marquee.text);
    u8g2_SetMaxClipWindow(u);
    g_marquee.lastOff = off;
}

} // namespace

bool panelName(Display& d, const char* number, const char* name, const char* sub,
               const Param& b, uint32_t nowMs)
{
    u8g2_t* u = d.raw();
    resetState(u);
    u8g2_SetFont(u, kFontValue);

    // The number first, in the value face, and the name a little to its
    // right in the name face, both on one baseline. Nothing here ever changes
    // size: the number is short by construction, the name scrolls when it has
    // to. The name region runs to the screen's edge - a marquee is clipped
    // there anyway, and a static name gains four pixels before it has to
    // scroll.
    int16_t nameX = 4;
    if (number != nullptr && number[0] != 0) {
        u8g2_DrawStr(u, 4, static_cast<u8g2_uint_t>(kNameBase), number);
        nameX = static_cast<int16_t>(4 + u8g2_GetStrWidth(u, number) + 6);
    }
    const int16_t width = static_cast<int16_t>(kW - nameX);
    if (name == nullptr) name = "";
    u8g2_SetFont(u, kFontName);

    bool scrolling = false;
    if (static_cast<int16_t>(u8g2_GetStrWidth(u, name)) <= width) {
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(nameX), static_cast<u8g2_uint_t>(kNameBase), name);
        g_marquee.active = false;
    } else {
        const bool fresh = !g_marquee.active
                        || std::strcmp(name, g_marquee.text) != 0
                        || nameX != g_marquee.x
                        || (nowMs - g_marquee.seenMs) > kAwayMs;
        if (fresh) {
            std::strncpy(g_marquee.text, name, sizeof(g_marquee.text) - 1);
            g_marquee.text[sizeof(g_marquee.text) - 1] = 0;
            g_marquee.x       = nameX;
            g_marquee.width   = width;
            g_marquee.textW   = static_cast<int16_t>(u8g2_GetStrWidth(u, g_marquee.text));
            g_marquee.startMs = nowMs;
            g_marquee.active  = true;
        }
        g_marquee.seenMs = nowMs;
        drawMarqueeBand(u, marqueeOffset(nowMs));
        scrolling = true;
    }

    if (sub != nullptr && sub[0] != 0) {
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, 4, 41, sub);
    }

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawHLine(u, 0, 44, kW);

    // The right-hand encoder's parameter, laid out along the line rather than
    // stacked: there is one of them and a whole width to put it in.
    if (b.name != nullptr && b.name[0] != 0) {
        int16_t bx = 4;
        if (b.norm >= 0.0f) {
            drawKnob(u, 13, 55, 7, b.norm);
            bx = 26;
        }
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(bx), 59, b.name);

        const char* bt = (b.text != nullptr) ? b.text : "";
        u8g2_SetFont(u, kFontValue);
        const int16_t tw = static_cast<int16_t>(u8g2_GetStrWidth(u, bt));
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(kW - 10 - tw), 59, bt);

        u8g2_SetFont(u, kFontSmall);
        u8g2_DrawStr(u, kW - 6, 62, "B");
    }

    resetState(u);
    return scrolling;
}

bool marqueeTick(Display& d, uint32_t nowMs)
{
    if (!g_marquee.active) return false;

    const int16_t off = marqueeOffset(nowMs);
    if (off == g_marquee.lastOff) return true;   // still holding, or between pixels: nothing to push

    u8g2_t* u = d.raw();
    // Clear the band from the name's left edge - the number to its left stays
    // in the buffer untouched - and redraw the name where it is now.
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, static_cast<u8g2_uint_t>(g_marquee.x), static_cast<u8g2_uint_t>(kNameBandTop),
                 static_cast<u8g2_uint_t>(kW - g_marquee.x),
                 static_cast<u8g2_uint_t>(kNameBandBottom - kNameBandTop + 1));
    drawMarqueeBand(u, off);
    resetState(u);
    g_marquee.seenMs = nowMs;
    d.flush(kNameBandTop, kNameBandBottom);
    return true;
}

// ---------------------------------------------------------------------------
// Body: lists
// ---------------------------------------------------------------------------

namespace {

// Four rows of 12 px below the header. The cursor row is inverted rather than
// marked with a caret: across a room an inverted bar is legible, a caret is
// not - the observation the pre-kit panels were built on.
constexpr int   kListRows  = 4;
constexpr int16_t kListPitch = 12;

void listColumn(u8g2_t* u, int16_t x, int16_t w, const char* const* entries,
                int count, int sel, bool focused)
{
    u8g2_SetFont(u, kFontList);

    // Keep the cursor one row from the top where there is room below, so the
    // list shows where it is going rather than only where it has been.
    int top = sel - 1;
    if (top > count - kListRows) top = count - kListRows;
    if (top < 0)                 top = 0;

    for (int i = 0; i < kListRows && top + i < count; ++i) {
        const int idx = top + i;
        const int16_t y = static_cast<int16_t>(kBodyTop + i * kListPitch);

        if (idx == sel) {
            if (focused) {
                u8g2_SetDrawColor(u, 1);
                u8g2_DrawBox(u, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y),
                             static_cast<u8g2_uint_t>(w), kListPitch);
                u8g2_SetDrawColor(u, 0);
            } else {
                // The unfocused column still marks its position, but as an
                // outline: two solid bars would leave the eye no idea which
                // one the encoder is actually moving.
                u8g2_SetDrawColor(u, 1);
                u8g2_DrawFrame(u, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y),
                               static_cast<u8g2_uint_t>(w), kListPitch);
            }
        }

        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + 3), static_cast<u8g2_uint_t>(y + 10),
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
    u8g2_DrawVLine(u, kSplit, kHeaderHeight, static_cast<u8g2_uint_t>(64 - kHeaderHeight));
    if (right != nullptr) {
        listColumn(u, kSplit + 2, kW - kSplit - 2, right, rightCount, rightSel, focusRight);
    }
    resetState(u);
}

// ---------------------------------------------------------------------------
// Diagnostics, About
// ---------------------------------------------------------------------------

void diagnostics(Display& d, const char* title, const char* const* rows, int count)
{
    u8g2_t* u = d.raw();

    d.clear();
    header(d, title);
    resetState(u);

    u8g2_SetFont(u, kFontLabel);
    if (count > kListRows) count = kListRows;
    for (int i = 0; i < count; ++i) {
        if (rows[i] == nullptr) continue;
        u8g2_DrawStr(u, 4, static_cast<u8g2_uint_t>(kBodyTop + i * kListPitch + 10), rows[i]);
    }
    resetState(u);
}

void about(Display& d, const char* name, const char* version, const char* hint)
{
    u8g2_t* u = d.raw();

    d.clear();
    header(d, "ABOUT");
    resetState(u);

    if (name != nullptr) {
        u8g2_SetFont(u, kFontValue);
        u8g2_DrawStr(u, 4, 30, name);
    }
    if (version != nullptr) {
        // Smaller than the name above it: the version is a git describe, and
        // "1.7.0-4-ga39504b" needs seventeen characters.
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, 4, 45, version);
    }
    if (hint != nullptr) {
        u8g2_SetFont(u, kFontLabel);
        u8g2_DrawStr(u, 4, 60, hint);
    }
    resetState(u);
}

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

void popup(Display& d, const char* title, const char* text)
{
    u8g2_t* u = d.raw();
    constexpr int16_t x = 8, y = 16, w = 112, h = 34;

    // Cleared first: a popup over a panel has to be opaque or neither is
    // readable.
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, x, y, w, h);

    u8g2_SetDrawColor(u, 1);
    u8g2_DrawFrame(u, x, y, w, h);
    u8g2_DrawBox(u, x, y, w, kHeaderHeight);

    u8g2_SetDrawColor(u, 0);
    u8g2_SetFont(u, kFontTitle);
    if (title != nullptr) u8g2_DrawStr(u, x + 3, y + 10, title);

    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, kFontValue);
    if (text != nullptr) {
        const int16_t tw = static_cast<int16_t>(u8g2_GetStrWidth(u, text));
        u8g2_DrawStr(u, static_cast<u8g2_uint_t>(x + (w - tw) / 2),
                     static_cast<u8g2_uint_t>(y + 29), text);
    }
    resetState(u);
}

} // namespace kit
} // namespace ui
} // namespace picoface
