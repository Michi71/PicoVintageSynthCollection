#ifndef __PICO_USERINTERFACE_H__
#define __PICO_USERINTERFACE_H__

#include <stdio.h>
#include "u8g2.h"
#include "encoder.h"
#include "push_button.h"


// Helper methods which are called from C code
#ifdef __cplusplus
extern "C" {
#endif

uint8_t pico_UserInterfaceSelectionList(u8g2_t *u8g2, Encoder *enc, PushButton *bt, const char *title, uint8_t start_pos, const char *sl);
uint8_t pico_UserInterfaceInputValue(u8g2_t *u8g2, Encoder *enc, PushButton *bt, const char *title, const char *pre, uint8_t *value, uint8_t lo, uint8_t hi, uint8_t digits, const char *post);

// Pump USB / MIDI / demo on Core 1 from inside blocking UI wait-loops.
void ui_poll_usb(void);
// Octave transpose (-2..+2), applied to incoming MIDI notes on Core 1.
// Set/read only from Core 1 (front panel + MIDI callbacks share the core).
void ui_set_octave(int oct);
int  ui_get_octave(void);
// Block (pumping USB) until the encoder button is released; stops one click
// cascading through several menu screens (ReadButton is level-triggered).
void ui_wait_button_release(PushButton* bt);

#ifdef __cplusplus
}
#endif

#endif // __PICO_USERINTERFACE_H__
