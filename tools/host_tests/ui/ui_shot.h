// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ui_shot.h - host-side stand-in for the OLED.
//
// u8g2 is set up for the same SH1106/SSD1306 128x64 full buffer the hardware
// uses, but with no byte transport behind it: nothing is sent, the buffer is
// written out as a PBM instead. Everything above the transport - fonts,
// glyph metrics, clipping, the draw calls themselves - is the same code that
// runs on the RP2350, so a screen rendered here is the screen on the panel.

#ifndef PICOFACE_UI_SHOT_H
#define PICOFACE_UI_SHOT_H

#include <cstdio>
#include <cstdlib>

#include "u8g2.h"

#include "picoface/ui.h"

// Owned by core/src/picoface_main.cpp on the target; the facade writes it to
// arm the staged flush, so the host build has to provide it.
uint8_t picoface_ui_flush_row = 16;

namespace uishot {

inline u8g2_t& context()
{
    static u8g2_t u;
    static bool ready = false;
    if (!ready) {
        u8g2_Setup_ssd1306_128x64_noname_f(&u, U8G2_R0, u8x8_byte_empty, u8x8_dummy_cb);
        u8g2_InitDisplay(&u);
        u8g2_SetPowerSave(&u, 0);
        ready = true;
    }
    return u;
}

inline picoface::ui::Display& display()
{
    static picoface::ui::Display d(&context());
    return d;
}

// The u8g2 full buffer is eight tile rows of 128 bytes, vertical, LSB = top.
// Written as plain-text PBM so a diff shows which pixels moved.
inline void save(const char* dir, const char* name)
{
    char path[256];
    std::snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot write %s\n", path);
        std::exit(1);
    }
    const uint8_t* b = u8g2_GetBufferPtr(&context());
    std::fprintf(f, "P1\n128 64\n");
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) {
            const uint8_t byte = b[(y / 8) * 128 + x];
            std::fputc(((byte >> (y & 7)) & 1) ? '1' : '0', f);
            std::fputc(x == 127 ? '\n' : ' ', f);
        }
    }
    std::fclose(f);
    std::printf("  %s\n", path);
}

inline void begin() { u8g2_ClearBuffer(&context()); }

} // namespace uishot

#endif // PICOFACE_UI_SHOT_H
