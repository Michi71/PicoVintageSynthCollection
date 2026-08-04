#ifndef DX_GUI_H
#define DX_GUI_H

#include "u8g2.h"
#include "DX_Controller.h"

// Reface DX algorithm diagram data (ported 1:1 from the ESP32 reference project).
static const int OP_PX = 13;

static const bool carriers[12][4] = {
  {1,0,0,0},
  {1,0,0,0},
  {1,0,0,0},
  {1,0,0,0},
  {1,0,0,0},
  {1,1,0,0},
  {1,1,0,0},
  {1,0,1,0},
  {1,1,1,0},
  {1,1,1,0},
  {1,1,1,0},
  {1,1,1,1}
};

static const uint8_t offsets[12][4] = {
  {0,1,1,1},
  {0,0,0,1},
  {0,1,1,0},
  {0,1,0,1},
  {0,2,1,0},
  {0,0,1,1},
  {0,0,1,1},
  {0,1,0,1},
  {0,0,0,2},
  {0,0,0,1},
  {0,0,0,1},
  {0,0,0,0}
};

// Draws the FM operator/algorithm diagram (carriers as boxes, modulators as
// circles) for algo_id (0..11) into a hTotal-pixel-high area starting at y0.
void drawAlgo(u8g2_t* u8g2, int y0, uint8_t algo_id, int hTotal, bool showId = false);

// Draws the current DX_Controller page: header + page name, and either the
// algorithm diagram (ALGO page) or a value placeholder (other pages).
void dxDrawScreen(u8g2_t* u8g2, DX_Controller& controller);

#endif // DX_GUI_H
