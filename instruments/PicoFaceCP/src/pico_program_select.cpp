#include "pico_userinterface.h"

#include "mdaEPiano.h"
#include "ipc.h"
#include "presets.h"
#include "midi_reface.h"

extern RefaceMidi refaceMidi;

/*
  Draw a string at x,y
  Center string within w (left adjust if w < pixel len of s)
  
  Side effects:
    u8g2_SetFontDirection(u8g2, 0);
    u8g2_SetFontPosBaseline(u8g2);

*/
void u8g2_SelDrawUTF8Line(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, const char *s, uint8_t border_size, uint8_t is_invert)
{
  u8g2_uint_t d, str_width;
  u8g2_uint_t fx, fy, fw, fh;

  /* only horizontal strings are supported, so force this here */
  u8g2_SetFontDirection(u8g2, 0);

  /* revert y position back to baseline ref */
  y += u8g2->font_calc_vref(u8g2);   

  /* calculate the width of the string in pixel */
  str_width = u8g2_GetUTF8Width(u8g2, s);

  /* calculate delta d within the box */
  d = 0;
  if ( str_width < w )
  {
    d = w;
    d -=str_width;
    d /= 2;
  }
  else
  {
    w = str_width;
  }

  /* caluclate text box */
  fx = x;
  fy = y - u8g2_GetAscent(u8g2) ;
  fw = w;
  fh = u8g2_GetAscent(u8g2) - u8g2_GetDescent(u8g2) ;

  /* draw the box, if inverted */
  u8g2_SetDrawColor(u8g2, 1);
  if ( is_invert )
  {
    u8g2_DrawBox(u8g2, fx, fy, fw, fh);
  }

  /* draw the frame */
  while( border_size > 0 )
  {
    fx--;
    fy--;
    fw +=2;
    fh +=2;
    u8g2_DrawFrame(u8g2, fx, fy, fw, fh );
    border_size--;
  }

  if ( is_invert )
  {
    u8g2_SetDrawColor(u8g2, 0);
  }
  else
  {
    u8g2_SetDrawColor(u8g2, 1);
  }

  /* draw the text */
  u8g2_DrawUTF8(u8g2, x+d, y, s);

  /* revert draw color */
  u8g2_SetDrawColor(u8g2, 1);

}

#ifdef __cplusplus
extern "C"
{
#endif

uint8_t pico_UserInterfaceProgramSelect(u8g2_t *u8g2, Encoder *enc, PushButton *bt, mdaEPiano *ep)
{
  (void)ep;
  u8g2_uint_t  xx;

  uint8_t local_value = preset_get_current();
  int32_t delta;
  char buf[24];

  /* only horizontal strings are supported, so force this here */
  u8g2_SetFontDirection(u8g2, 0);

  /* force baseline position */
  u8g2_SetFontPosBaseline(u8g2);

  /* event loop */
  for(;;)
  {
    u8g2_FirstPage(u8g2);
    do
    {
      /* render */
      u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
      strcpy(buf, "PRESET");
      u8g2_SelDrawUTF8Line(u8g2, 0, 10, u8g2_GetDisplayWidth(u8g2)-2, buf, 0, 0);
      strncpy(buf, cpPresets[local_value].name, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = 0;
      u8g2_SelDrawUTF8Line(u8g2, 0, 60, u8g2_GetDisplayWidth(u8g2)-2, buf, 0, 0);

      u8g2_DrawHLine(u8g2, 0, 12, u8g2_GetDisplayWidth(u8g2));

      /* Preset number */
      xx = 0;
      u8g2_SetFont(u8g2, u8g2_font_fub25_tf);
      xx += u8g2_DrawUTF8(u8g2, xx, 44, "P");
      u8g2_DrawUTF8(u8g2, xx, 44, u8x8_u8toa(local_value, 3));

    } while( u8g2_NextPage(u8g2) );

    for(;;)
    {
      ui_poll_usb(); delta = enc->delta();
      if (bt->ReadButton() == PushButton::PRESSED)
      {
        ui_wait_button_release(bt); return local_value;
      }
      else if (delta > 0)
      {
        if ( local_value >= CP_NPRESETS - 1 )
          local_value = 0;
        else
          local_value++;
        preset_set_current(local_value);
        ipc_send_program(local_value);
        refaceMidi.txProgram(local_value);
        break;
      }
      else if (delta < 0)
      {
        if ( local_value <= 0 )
          local_value = CP_NPRESETS - 1;
        else
          local_value--;
        preset_set_current(local_value);
        ipc_send_program(local_value);
        refaceMidi.txProgram(local_value);
        break;
      }
    }
  }
}

uint8_t pico_UserInterfaceInstrumentSelect(u8g2_t *u8g2, Encoder *enc, PushButton *bt, mdaEPiano *ep) {
    char buf[128];
    u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);

    int32_t count = ep->getInstrumentCount();

    buf[0] = 0;
    for (int32_t i = 0; i < count; i++) {
        strcat(buf, ep->getInstrumentName(i));
        strcat(buf, "\n");
    }
    strcat(buf, "<< BACK");

    uint8_t start = (uint8_t)(ep->getCurrentInstrument() + 1);
    uint8_t sel = pico_UserInterfaceSelectionList(u8g2, enc, bt, "INSTRUMENT", start, buf);

    if (sel >= 1 && sel <= (uint8_t)count) {
        ipc_send_instrument((uint8_t)(sel - 1));
        return (uint8_t)(sel - 1);
    }

    return 0xFF;
}

#ifdef __cplusplus
}
#endif