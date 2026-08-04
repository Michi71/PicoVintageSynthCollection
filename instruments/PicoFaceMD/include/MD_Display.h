// MD_Display.h — Pure-visual display module for PicoFaceMD (RP2350 + SH1106 128x64 via u8g2 C-API).
// Mirrors the PicoFaceCP look: inverted header bar + separator, page indicator,
// body lines and footer. Content strings are preformatted by the caller (md_main.cpp).
//
// Two body layouts, both sharing the same header and footer:
//   md_display_page  two labelled values, one per encoder — the parameter pages
//   md_display_list  three rows with a cursor — the section menu and the preset list
#pragma once
#include <stdint.h>
#include "u8g2.h"

struct MdUiModel {
    char title[16];   // header left, e.g. "MOD FILTER"
    char page[8];     // header right, e.g. "1/5"
    char lineA[26];   // body line at y=32 (font 8x13B)
    char lineB[26];   // body line at y=48 (font 8x13B)
    char footer[26];  // footer at y=62 (font 6x10), diagnostics
};

struct MdListModel {
    char    title[16];    // header left, e.g. "MENU"
    char    page[8];      // header right, e.g. "3/8"
    char    rows[3][22];  // three visible entries, top to bottom
    uint8_t cursor;       // which of the three is selected, 0..2
    char    footer[26];   // footer at y=62 (font 6x10), diagnostics
};

void md_display_splash(u8g2_t* u); // boot only: draws logo + BLOCKING SendBuffer
void md_display_page(u8g2_t* u, const MdUiModel& m); // draws into buffer, NO send
void md_display_list(u8g2_t* u, const MdListModel& m); // draws into buffer, NO send
