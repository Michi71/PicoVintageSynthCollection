// -----------------------------------------------------------------------------
// picoface/list_view.h
//
// Non-blocking selection list for the 128x64 OLED, part of the optional core
// module "ui_menu".
//
// The blocking counterpart in the ui_panel module
// (pico_UserInterfaceSelectionList) owns the encoders and spins inside its own
// loop until the user picks an entry. This one holds nothing but cursor state:
// an instrument feeds it one InputState per uiTick() and draws it, so the audio
// producer keeps running while a menu is open.
// -----------------------------------------------------------------------------

#ifndef PICOFACE_LIST_VIEW_H
#define PICOFACE_LIST_VIEW_H

#include <cstdint>

#include "picoface/ui.h"

namespace picoface {
namespace ui {

class ListView {
public:
    // update() returns this while the user is still browsing.
    static constexpr int kNone = -1;

    // (Re)opens the list on 'entries'. The array must outlive the view - a
    // static table of string literals is the intended use.
    void open(const char* const* entries, uint8_t count, uint8_t cursor = 0);

    // Consumes one input snapshot: the Sel encoder moves the cursor, the Sel
    // button picks. Returns the chosen index or kNone.
    // The cursor clamps at both ends instead of wrapping, so a "<< BACK" entry
    // at the end stays where the user last left it.
    int update(const InputState& in);

    // Draws the title bar and three rows, the cursor row inverted. Clears the
    // buffer first and does NOT flush - the caller decides when to push.
    void draw(Display& d, const char* title) const;

    uint8_t cursor() const { return cursor_; }

private:
    const char* const* entries_ = nullptr;
    uint8_t            count_   = 0;
    uint8_t            cursor_  = 0;
};

} // namespace ui
} // namespace picoface

#endif // PICOFACE_LIST_VIEW_H
