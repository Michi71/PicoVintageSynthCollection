// list_view.cpp - implementation of picoface::ui::ListView (module ui_menu).
//
// Drawn through u8g2 directly rather than through the Display facade: the
// cursor row needs an inverted draw color, which the facade deliberately does
// not expose. Same arrangement as core/src/ui/display.cpp.

#include "picoface/list_view.h"

#include "u8g2.h"

namespace picoface {
namespace ui {

namespace {
// Title baseline, separator, and the three row baselines. Matches the layout
// of the paged front panels so a menu does not jump against its home screen.
constexpr int16_t kTitleBaseline = 14;
constexpr int16_t kSeparatorY    = 18;
constexpr int16_t kFirstRowY     = 32;
constexpr int16_t kRowHeight     = 14;
constexpr int16_t kVisibleRows   = 3;
} // namespace

void ListView::open(const char* const* entries, uint8_t count, uint8_t cursor)
{
    entries_ = entries;
    count_   = count;
    if (count == 0) {
        cursor_ = 0;
    } else {
        cursor_ = (cursor < count) ? cursor : static_cast<uint8_t>(count - 1);
    }
}

int ListView::update(const InputState& in)
{
    if (count_ == 0) {
        return kNone;
    }

    const int8_t d = in.delta(Encoder::Sel);
    if (d != 0) {
        int c = static_cast<int>(cursor_) + d;
        if (c < 0) {
            c = 0;
        }
        if (c > count_ - 1) {
            c = count_ - 1;
        }
        cursor_ = static_cast<uint8_t>(c);
    }

    if (in.pressed(Button::Sel)) {
        return static_cast<int>(cursor_);
    }
    return kNone;
}

void ListView::draw(Display& d, const char* title) const
{
    u8g2_t* u = d.raw();

    d.clear();
    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);

    if (title != nullptr) {
        u8g2_DrawStr(u, 4, kTitleBaseline, title);
    }
    u8g2_DrawHLine(u, 0, kSeparatorY, Display::kWidth);

    // Keep the cursor in the middle row wherever there is room above and below.
    int top = static_cast<int>(cursor_) - 1;
    if (top > static_cast<int>(count_) - kVisibleRows) {
        top = static_cast<int>(count_) - kVisibleRows;
    }
    if (top < 0) {
        top = 0;
    }

    for (int row = 0; row < kVisibleRows; ++row) {
        const int idx = top + row;
        if (idx >= static_cast<int>(count_)) {
            break;
        }

        const int16_t y = static_cast<int16_t>(kFirstRowY + row * kRowHeight);
        if (idx == static_cast<int>(cursor_)) {
            u8g2_DrawBox(u, 0, static_cast<u8g2_uint_t>(y - 11), Display::kWidth, kRowHeight);
            u8g2_SetDrawColor(u, 0);
        }
        u8g2_DrawStr(u, 4, static_cast<u8g2_uint_t>(y), entries_[idx] ? entries_[idx] : "");
        u8g2_SetDrawColor(u, 1);
    }
}

} // namespace ui
} // namespace picoface
