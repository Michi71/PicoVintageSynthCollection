// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "picoface/ui.h"

#include "u8g2.h"

// Owned by core/src/picoface_main.cpp; 16 = idle, 0 arms the incremental push.
extern uint8_t picoface_ui_flush_row;

namespace picoface {
namespace ui {

Display::Display(DisplayHandle u8g2) : u8g2_(u8g2) {}

DisplayHandle Display::raw() const {
  return u8g2_;
}

void Display::clear() {
  u8g2_ClearBuffer(u8g2_);
}

void Display::flush() {
  // Does NOT call u8g2_SendBuffer. A blocking full-buffer transfer takes far
  // too long and would starve the audio producer; the main loop pushes the
  // buffer out in half tile rows instead.
  picoface_ui_flush_row = 0;
}

void Display::setFont(const uint8_t* font) {
  u8g2_SetFont(u8g2_, font);
}

void Display::drawText(int16_t x, int16_t y, const char* text) {
  u8g2_DrawStr(u8g2_, static_cast<u8g2_uint_t>(x),
               static_cast<u8g2_uint_t>(y), text);
}

void Display::drawTextCentered(int16_t y, const char* text) {
  const int16_t w = textWidth(text);
  const int16_t x = static_cast<int16_t>((kWidth - w) / 2);
  u8g2_DrawStr(u8g2_, static_cast<u8g2_uint_t>(x),
               static_cast<u8g2_uint_t>(y), text);
}

int16_t Display::textWidth(const char* text) const {
  // u8g2_GetStrWidth only reads, but its C signature is non-const.
  return static_cast<int16_t>(
      u8g2_GetStrWidth(const_cast<DisplayHandle>(u8g2_), text));
}

void Display::drawFrame(int16_t x, int16_t y, int16_t w, int16_t h) {
  u8g2_DrawFrame(u8g2_, static_cast<u8g2_uint_t>(x),
                 static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w),
                 static_cast<u8g2_uint_t>(h));
}

void Display::drawBox(int16_t x, int16_t y, int16_t w, int16_t h) {
  u8g2_DrawBox(u8g2_, static_cast<u8g2_uint_t>(x),
               static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w),
               static_cast<u8g2_uint_t>(h));
}

void Display::drawHLine(int16_t x, int16_t y, int16_t w) {
  u8g2_DrawHLine(u8g2_, static_cast<u8g2_uint_t>(x),
                 static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w));
}

void Display::drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                         const uint8_t* bits) {
  // XBM bit order matches the logo headers already used by the instruments.
  u8g2_DrawXBM(u8g2_, static_cast<u8g2_uint_t>(x),
               static_cast<u8g2_uint_t>(y), static_cast<u8g2_uint_t>(w),
               static_cast<u8g2_uint_t>(h), bits);
}

}  // namespace ui
}  // namespace picoface
