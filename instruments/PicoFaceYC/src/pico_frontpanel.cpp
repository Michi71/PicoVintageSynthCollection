
// pico_frontpanel.cpp
//
// Three-encoder paged "virtual front panel" for the Reface YC
// (RP2350 + SH1106 128x64 OLED).
//
//   - The SELECTOR encoder pages through the YC pages (see YcPage).
//   - The PARAM A and PARAM B encoders edit the two values of the current page.
//   - Pressing PARAM A toggles Percussion On/Off while on the PERCUSSION page.
//   - A LONG press (>= 500 ms) of the selector opens the System main menu.

#include "pico_frontpanel.h"
#include "pico_userinterface.h"
#include "pico/stdlib.h"
#include "hardware/structs/watchdog.h"
#include "YC_GUI.h"
#include "YC_Synth_Bridge.h"
#include <cstdio>
#include <cstring>

extern YC_Controller ycController;
extern YC_Synth_Bridge ycBridge;

#define LONG_PRESS_MS 500

static void showAbout(u8g2_t* u, Encoder* enc, PushButton* bt)
{
    (void)enc;
    for(;;){
        u8g2_FirstPage(u);
        do {
            u8g2_SetFont(u, u8g2_font_8x13B_tf);
            u8g2_SetFontPosBaseline(u);
            u8g2_DrawStr(u, 4, 14, "ABOUT");
            u8g2_DrawHLine(u, 0, 18, 128);
#ifdef PICO_PROGRAM_NAME
            u8g2_DrawStr(u, 4, 36, PICO_PROGRAM_NAME);
#else
            u8g2_DrawStr(u, 4, 36, "Reface DX");
#endif
#ifdef PICO_PROGRAM_VERSION_STRING
            u8g2_DrawStr(u, 4, 52, PICO_PROGRAM_VERSION_STRING);
#else
            u8g2_DrawStr(u, 4, 52, "v1.0");
#endif
            u8g2_SetFont(u, u8g2_font_6x10_tf);
            u8g2_DrawStr(u, 4, 62, "Press any button");
        } while (u8g2_NextPage(u));

        ui_poll_usb();
        if (bt->ReadButton() == PushButton::PRESSED) {
            ui_wait_button_release(bt);
            break;
        }
    }
}

static void showCpuLoad(u8g2_t* u, Encoder* enc, PushButton* bt)
{
    (void)enc;
    char buf[32];
    for(;;){
        u8g2_FirstPage(u);
        do {
            u8g2_SetFont(u, u8g2_font_8x13B_tf);
            u8g2_SetFontPosBaseline(u);
            u8g2_DrawStr(u, 4, 14, "CPU LOAD");
            u8g2_DrawHLine(u, 0, 18, 128);
            u8g2_SetFont(u, u8g2_font_6x10_tf);
            snprintf(buf, sizeof(buf), "Now:  %d %%", (int)ycBridge.cpuLoadPercent());
            u8g2_DrawStr(u, 4, 36, buf);
            snprintf(buf, sizeof(buf), "Peak: %d %%", (int)ycBridge.cpuLoadPeakPercent());
            u8g2_DrawStr(u, 4, 52, buf);
            snprintf(buf, sizeof(buf), "WDR:  %lu", (unsigned long)watchdog_hw->scratch[0]);
            u8g2_DrawStr(u, 4, 62, buf);
        } while (u8g2_NextPage(u));

        ui_poll_usb();
        if (bt->ReadButton() == PushButton::PRESSED) {
            ui_wait_button_release(bt);
            break;
        }
    }
}

static void openSystem(u8g2_t* u, Encoder* enc, PushButton* bt)
{
    uint8_t sel = pico_UserInterfaceSelectionList(u, enc, bt, "SYSTEM", 1, "About\nCPU Load\n<< BACK");
    if (sel == 1) showAbout(u, enc, bt);
    else if (sel == 2) showCpuLoad(u, enc, bt);
}

static void openMainMenu(u8g2_t* u, Encoder* enc, PushButton* bt)
{
    uint8_t sel = pico_UserInterfaceSelectionList(u, enc, bt, "MENU", 1, "System\n<< BACK");
    if (sel == 1)      openSystem(u, enc, bt);
}

void pico_UserInterfaceFrontPanel(u8g2_t* u8g2, Encoder* encSel, PushButton* btSel,
                                  Encoder* encA,  PushButton* btA,
                                  Encoder* encB,  PushButton* btB)
{
    (void)btB;
    bool selHeld = false;
    uint32_t selPressT = 0;
    static uint32_t lastRedraw = 0;

    for (;;) {
        ui_poll_usb();
        uint32_t now = to_ms_since_boot(get_absolute_time());

        bool selState = btSel->ReadButton();
        if (selState == PushButton::PRESSED) {
            if (!selHeld) { selHeld = true; selPressT = now; }
        } else {
            if (selHeld) {
                uint32_t dur = now - selPressT;
                selHeld = false;
                if (dur >= LONG_PRESS_MS) {
                    openMainMenu(u8g2, encSel, btSel);
                }
                continue;
            }
        }

        static bool aHeld = false;
        if (btA->ReadButton() == PushButton::PRESSED) {
            if (!aHeld) {
                aHeld = true;
                ycController.onButtonA();
            }
        } else {
            aHeld = false;
        }

        int32_t dSel = encSel->delta();
        int32_t dA   = encA->delta();
        int32_t dB   = encB->delta();
        if (dSel != 0) ycController.onEncoder1(dSel);
        if (dA   != 0) ycController.onEncoder2(dA);
        if (dB   != 0) ycController.onEncoder3(dB);

        if ((now - lastRedraw) >= 33) {
            u8g2_FirstPage(u8g2);
            do {
                ycDrawScreen(u8g2, ycController);
            } while (u8g2_NextPage(u8g2));
            lastRedraw = now;
        }
    }
}

