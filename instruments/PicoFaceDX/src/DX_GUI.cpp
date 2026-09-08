// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "DX_GUI.h"

#include "picoface/ui_kit.h"
#include "dx_engine/DX_FXHost.h"
#include <cstdio>

// Thin call-syntax adapters: ESP32 reference used display.method(...) on a
// custom UI_Display class; here we call u8g2 directly with the same
// geometry/arguments (only the call syntax changes, math is untouched).
static inline void drawRect(u8g2_t* u, int x, int y, int w, int h) { u8g2_DrawFrame(u, x, y, w, h); }
static inline void drawHLine(u8g2_t* u, int x, int y, int len) { if (len > 0) u8g2_DrawHLine(u, x, y, len); }
static inline void drawVLine(u8g2_t* u, int x, int y, int len) { if (len > 0) u8g2_DrawVLine(u, x, y, len); }
static inline void drawCircle(u8g2_t* u, int x0, int y0, int diameter) { u8g2_DrawCircle(u, x0, y0, diameter / 2, U8G2_DRAW_ALL); }
static inline void drawChar(u8g2_t* u, int x, int y, char c) { u8g2_DrawGlyph(u, x, y, (uint16_t)(unsigned char)c); }
static inline void drawText(u8g2_t* u, int x, int y, const char* s) { u8g2_DrawStr(u, x, y, s); }
static inline int getFontWidth(u8g2_t* u) { return u8g2_GetMaxCharWidth(u); }
static inline int getFontHeight(u8g2_t* u) { return u8g2_GetAscent(u) - u8g2_GetDescent(u); }
static inline int getDisplayWidth(u8g2_t* u) { (void)u; return 128; } // SH1106 128x64

void drawAlgo(u8g2_t* u8g2, int y0, uint8_t algo_id, int hTotal, bool showId) {
  bool compact = false;
  int8_t maxCarrier = 0;
  int vSink = 4;
  int labelOff = 3;
  int8_t x[4], y[4];
  int fh2 = getFontHeight(u8g2)/2;
  int fw2 = getFontWidth(u8g2)/2;
  int nn = getDisplayWidth(u8g2) / 4;
  int x0 = nn / 2;
  int ww = OP_PX / 2;
  int lh = showId ? getFontHeight(u8g2) + labelOff : 0;
  int hMax = hTotal - lh;
  int vLink = (hMax - 3 * OP_PX - vSink) / 2;
  if (vLink < 0) { compact = true; vLink = (hMax - OP_PX) / 2; }
  int8_t yy = y0 + hMax - OP_PX / 2 - vSink ;
  u8g2_SetDrawColor(u8g2, 1);
  for ( int i=0; i<4; i++ ) {
    x[i] = i * nn + x0;
    if (compact) { y[i] = yy ; } else { y[i] = yy - (OP_PX + vLink) * offsets[algo_id][i] ; }
  }
  for (uint8_t id = 0 ; id < 4 ; id++) {
    drawChar(u8g2, x[id] - fw2 + 1, y[id] + fh2, static_cast<char>(id + '1'));
    if (carriers[algo_id][id]) {
      maxCarrier = id;
      drawRect(u8g2, x[id] - ww, y[id] - ww, OP_PX, OP_PX);
      drawVLine(u8g2, x[id], y[id] + ww, vSink+1);
    } else { drawCircle(u8g2, x[id], y[id], OP_PX ); }
  }
  if (maxCarrier > 0) { drawHLine(u8g2, nn/2, yy + ww + vSink, maxCarrier * nn + 1); }
  if (showId) {
    int offset = algo_id>=9 ? getFontWidth(u8g2) : fw2;
    char idBuf[4]; snprintf(idBuf, sizeof(idBuf), "%d", algo_id + 1);
    drawText(u8g2, x[0] + (x[maxCarrier] - x[0]) / 2 - offset, y0 + hTotal - getFontHeight(u8g2) + 6, idBuf);
  }
  switch(algo_id){
    case 0:
      drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
      drawHLine(u8g2, x[1] + ww, y[1], nn - OP_PX + 1);
      if (compact) { drawHLine(u8g2, x[0] + ww, y[0], nn - OP_PX + 1); }
      else { drawHLine(u8g2, x[0] , y[1], nn - ww + 1); drawVLine(u8g2, x[0] , y[1], ww + vLink + 1 ); }
      break;
    case 1:
      drawHLine(u8g2, x[1] + ww, y[1], nn - OP_PX + 1);
      drawHLine(u8g2, x[0] + ww, y[0], nn - OP_PX + 1);
      if (compact) {
        drawHLine(u8g2, x[1], y[0] - ww - vLink, x[3] - x[1]);
        drawVLine(u8g2, x[1], y[0] - ww - vLink, vLink);
        drawVLine(u8g2, x[3], y[0] - ww - vLink, vLink);
      } else { drawHLine(u8g2, x[1], y[3], x[3] - x[1] - ww + 1); drawVLine(u8g2, x[1], y[3], ww + vLink + 1); }
      break;
    case 2:
      drawHLine(u8g2, x[1] + ww, y[1], nn - OP_PX + 1);
      if (compact) {
        drawHLine(u8g2, x[0] + ww, y[0], nn - OP_PX + 1);
        drawHLine(u8g2, x[0], y[0] - ww - vLink, x[3] - x[0]);
        drawVLine(u8g2, x[0], y[0] - ww - vLink, vLink);
        drawVLine(u8g2, x[3], y[0] - ww - vLink, vLink);
      } else {
        drawHLine(u8g2, x[0], y[1], nn - ww + 1);
        drawVLine(u8g2, x[0], y[1], ww + vLink + 1 );
        drawHLine(u8g2, x[0] + ww, y[0], x[3] - x[0] - OP_PX + 1);
      }
      break;
    case 3:
      if (compact) {
        drawHLine(u8g2, x[0] , y[0] - ww - vLink, x[2] - x[0]);
        drawVLine(u8g2, x[0] , y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[2] , y[0] - ww - vLink, vLink+1);
        drawHLine(u8g2, x[1] , y[0] + ww + vLink, x[3] - x[1]);
        drawVLine(u8g2, x[1] , y[0] + ww, vLink+1);
        drawVLine(u8g2, x[3] , y[0] + ww, vLink+1);
        drawHLine(u8g2, x[0] + ww, y[0], nn - OP_PX + 1);
        drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
      } else {
        drawHLine(u8g2, x[0] , y[1], nn - ww + 1);
        drawVLine(u8g2, x[0] , y[1], ww + vLink + 1 );
        drawHLine(u8g2, x[0] + ww, y[0], 2 * nn - OP_PX + 1);
        drawHLine(u8g2, x[1] + ww, y[1], 2 * nn - OP_PX + 1);
        drawHLine(u8g2, x[2] + ww, y[2], nn - ww + 1);
        drawVLine(u8g2, x[3] , y[3] + ww, ww + vLink + 1 );
      }
      break;
    case 4:
      if (compact) {
        drawHLine(u8g2, x[0] , y[0] - ww - vLink, x[3] - x[0]);
        drawVLine(u8g2, x[0] , y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[1] , y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[2] , y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[3] , y[0] - ww - vLink, vLink+1);
      } else {
        drawHLine(u8g2, x[0] , y[2], x[2] - x[0] - ww + 1);
        drawVLine(u8g2, x[0] , y[2], ww + vLink + 1 );
        drawHLine(u8g2, x[0] + ww, y[0], x[3] - x[0] - OP_PX + 1);
        drawHLine(u8g2, x[0] - ww - vSink, y[1], nn + vSink + 1);
        drawHLine(u8g2, x[0] - ww - vSink, y[0], vSink + 1 );
        drawVLine(u8g2, x[0] - ww - vSink, y[1], y[0] - y[1] + 1);
      }
      break;
    case 5:
      drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
      if (compact) { drawHLine(u8g2, x[1] + ww, y[1], nn - OP_PX + 1); }
      else { drawHLine(u8g2, x[1] , y[2], x[1] - x[0] - ww + 1); drawVLine(u8g2, x[1] , y[2], ww + vLink + 1 ); }
      break;
    case 6:
      drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
      if (compact) {
        drawHLine(u8g2, x[1] + ww, y[1], nn - OP_PX + 1);
        drawHLine(u8g2, x[0], y[0] - ww - vLink, x[2] - x[0]);
        drawVLine(u8g2, x[0], y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[2], y[0] - ww - vLink, vLink+1);
      } else {
        drawHLine(u8g2, x[0] , y[2], x[2] - x[0] - ww + 1);
        drawVLine(u8g2, x[0] , y[2], ww + vLink + 1 );
        drawHLine(u8g2, x[1] + ww , y[1], x[2] - x[1] - ww + 1);
        drawVLine(u8g2, x[2] , y[2] + ww, ww + vLink + 1 );
      }
      break;
    case 7:
      if (compact) {
        drawHLine(u8g2, x[0] + ww, y[0], nn - OP_PX + 1);
        drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
      } else {
        drawHLine(u8g2, x[0] , y[1], nn - ww + 1);
        drawVLine(u8g2, x[0] , y[1], ww + vLink + 1 );
        drawHLine(u8g2, x[2] , y[1], nn - ww + 1);
        drawVLine(u8g2, x[2] , y[1], ww + vLink + 1 );
      }
      break;
    case 8:
      if (compact) {
        drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
        drawVLine(u8g2, x[0] , y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[1] , y[0] - ww - vLink, vLink+1);
        drawVLine(u8g2, x[3] , y[0] - ww - vLink, vLink+1);
        drawHLine(u8g2, x[0] , y[0] - ww - vLink, x[3] - x[0]);
      } else {
        drawHLine(u8g2, x[0], y[3], x[3] - x[0] - ww + 1);
        drawVLine(u8g2, x[0], y[3], y[0] - y[3] - ww + 1);
        drawHLine(u8g2, x[1] , y[3] + OP_PX + vLink, x[3] - x[1] + 1);
        drawVLine(u8g2, x[3] , y[3] + ww, OP_PX + vLink - ww + 1 );
        drawVLine(u8g2, x[1] , y[3] + OP_PX + vLink , OP_PX + vLink - ww + 1 );
        drawHLine(u8g2, x[2] + ww, y[2], x[3] - x[2] + vSink + 1);
        drawHLine(u8g2, x[3] + ww, y[3], vSink + 1);
        drawVLine(u8g2, x[3] + ww + vSink, y[3], y[0] - y[3] + 1);
      }
      break;
    case 9:
      if (compact) {
        drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1);
        drawHLine(u8g2, x[1], y[0] - ww - vLink, x[3] - x[1]);
        drawVLine(u8g2, x[1], y[0] - ww - vLink, vLink);
        drawVLine(u8g2, x[3], y[0] - ww - vLink, vLink);
      } else {
        drawHLine(u8g2, x[1], y[3], x[3] - x[1] - ww + 1);
        drawVLine(u8g2, x[1], y[3], ww + vLink + 1);
        drawHLine(u8g2, x[2] + ww, y[2], nn - ww + 1);
        drawVLine(u8g2, x[3] , y[3] + ww, ww + vLink + 1 );
      }
      break;
    case 10:
      if (compact) { drawHLine(u8g2, x[2] + ww, y[2], nn - OP_PX + 1); }
      else { drawHLine(u8g2, x[2] , y[3], nn - ww + 1); drawVLine(u8g2, x[2] , y[3], ww + vLink + 1 ); }
      break;
    case 11:
    default:
      break;
  }
}

// The page as the shared kit lays it out: header, then either the two values
// the encoders edit or, on the ALGO page, the diagram above -- which is the
// whole point of keeping an instrument-owned body. drawAlgo() is untouched.
void dxDrawScreen(picoface::ui::Display& d, DX_Controller& controller) {
    namespace kit = picoface::ui::kit;

    kit::header(d, controller.pageName(), (int) controller.currentPage(),
                (int) DxPage::COUNT);

    char va[32], vb[32];

    switch (controller.currentPage()) {
    case DxPage::ALGO:
        // Everything between kBodyTop and kBodyBottom belongs to the
        // instrument; the algorithm diagram is what that is for.
        drawAlgo(d.raw(), kit::kBodyTop, controller.patch().common.algorithm,
                 kit::kBodyHeight, true);
        break;

    case DxPage::FX1:
    case DxPage::FX2: {
        const int slot = (controller.currentPage() == DxPage::FX1) ? 0 : 1;
        int typeId = controller.patch().common.effects[slot][0];
        if (typeId >= FX_COUNT) typeId = FX_THRU;
        const int param1 = controller.patch().common.effects[slot][1];

        snprintf(va, sizeof(va), "%s", FX_NAMES[typeId]);
        if (typeId == FX_THRU) {
            // Nothing to set while the slot is bypassed.
            kit::panelDuo(d, { "Type", va }, {});
        } else {
            snprintf(vb, sizeof(vb), "%d", param1);
            kit::panelDuo(d, { "Type", va },
                             { FX_PARAM1_LABELS[typeId], vb, param1 / 127.0f });
        }
        break;
    }

    case DxPage::OP1:
    case DxPage::OP2:
    case DxPage::OP3:
    case DxPage::OP4: {
        const int opIdx = static_cast<int>(controller.currentPage());
        const int coarse = controller.patch().ops[opIdx].freqCoarse;
        const int level  = controller.patch().ops[opIdx].outLevel;
        snprintf(va, sizeof(va), "%d", coarse);
        snprintf(vb, sizeof(vb), "%d", level);
        kit::panelDuo(d, { "Freq", va, coarse / 31.0f },
                         { "Level", vb, level / 127.0f });
        break;
    }

    case DxPage::LFO: {
        const int speed = controller.patch().common.lfoSpeed;
        const int pmd   = controller.patch().common.lfoPMD;
        snprintf(va, sizeof(va), "%d", speed);
        snprintf(vb, sizeof(vb), "%d", pmd);
        kit::panelDuo(d, { "Speed", va, speed / 127.0f },
                         { "PMD", vb, pmd / 127.0f });
        break;
    }

    default:
        break;
    }
}
