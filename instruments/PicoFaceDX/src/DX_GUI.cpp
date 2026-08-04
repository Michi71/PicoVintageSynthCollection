#include "DX_GUI.h"
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

void dxDrawScreen(u8g2_t* u8g2, DX_Controller& controller) {
    u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u8g2);
    u8g2_DrawStr(u8g2, 4, 14, controller.pageName());
    u8g2_DrawHLine(u8g2, 0, 18, 128);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

    if (controller.currentPage() == DxPage::ALGO) {
        drawAlgo(u8g2, 20, controller.patch().common.algorithm, 40, true);
    } else if (controller.currentPage() == DxPage::FX1 || controller.currentPage() == DxPage::FX2) {
        int slot = (controller.currentPage() == DxPage::FX1) ? 0 : 1;
        int typeId = controller.patch().common.effects[slot][0];
        if (typeId >= FX_COUNT) typeId = FX_THRU;
        int param1 = controller.patch().common.effects[slot][1];
        char buf[32];
        snprintf(buf, sizeof(buf), "Type: %s", FX_NAMES[typeId]);
        u8g2_DrawStr(u8g2, 4, 40, buf);
        if (typeId != FX_THRU) {
            snprintf(buf, sizeof(buf), "%s: %d", FX_PARAM1_LABELS[typeId], param1);
            u8g2_DrawStr(u8g2, 4, 54, buf);
        }
    } else if (controller.currentPage() == DxPage::OP1 || controller.currentPage() == DxPage::OP2 || controller.currentPage() == DxPage::OP3 || controller.currentPage() == DxPage::OP4) {
        int opIdx = static_cast<int>(controller.currentPage());
        char buf[32];
        snprintf(buf, sizeof(buf), "Freq: %d", controller.patch().ops[opIdx].freqCoarse);
        u8g2_DrawStr(u8g2, 4, 40, buf);
        snprintf(buf, sizeof(buf), "Level: %d", controller.patch().ops[opIdx].outLevel);
        u8g2_DrawStr(u8g2, 4, 54, buf);
    } else if (controller.currentPage() == DxPage::LFO) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Speed: %d", controller.patch().common.lfoSpeed);
        u8g2_DrawStr(u8g2, 4, 40, buf);
        snprintf(buf, sizeof(buf), "PMD: %d", controller.patch().common.lfoPMD);
        u8g2_DrawStr(u8g2, 4, 54, buf);
    } else {
        u8g2_DrawStr(u8g2, 4, 40, "(values shown via encoders)");
    }
}
