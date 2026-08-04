// pico_frontpanel.cpp
//
// Three-encoder paged "virtual front panel" for the Reface CP
// (RP2350 + SH1106 128x64 OLED).
//
//   - SELECTOR encoder pages through the screens (one parameter set each).
//   - SELECTOR short press cycles the effect mode on the TREM / CHO / DLY
//     screens (Off -> A -> B); long press (>= 500 ms) opens the main menu.
//   - PARAM A and PARAM B encoders set the two on-screen values; their
//     optional switches reset that value to a sensible default.

#include "pico_frontpanel.h"
#include "pico_userinterface.h"
#include "pico/stdlib.h"
#include "ipc.h"
#include "midi_reface.h"
#include <cstdio>
#include <cstring>

/* ------------------------------------------------------------------ */
/* Static helpers                                                     */
/* ------------------------------------------------------------------ */
static inline float clamp01f(float v){return v<0.0f?0.0f:(v>1.0f?1.0f:v);}
static int pct(float v){int x=(int)(v*100.0f+0.5f); if(x<0)x=0; if(x>100)x=100; return x;}

// panel change -> IPC to Core 0 + MIDI OUT CC (reface CP MIDI control)
extern RefaceMidi refaceMidi;
static void fp_send_fx_param(uint8_t id, float v){ ipc_send_fx_param(id, v); refaceMidi.txFxParam(id, v); }
static void fp_send_fx_mode(uint8_t id, uint8_t m){ ipc_send_fx_mode(id, m); refaceMidi.txFxMode(id, (int)m); }
static void fp_send_instrument(int i){ ipc_send_instrument((uint8_t)i); refaceMidi.txInstrument(i); }

/* ------------------------------------------------------------------ */
/* Menu helpers (use selector encoder / button)                       */
/* ------------------------------------------------------------------ */
static void showAbout(u8g2_t* u, Encoder* enc, PushButton* bt)
{
    (void)enc;
    // Draw ABOUT screen using project name/version macros
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
            u8g2_DrawStr(u, 4, 36, "Reface CP");
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

static void openSystem(u8g2_t* u, Encoder* enc, PushButton* bt)
{
    uint8_t sel = pico_UserInterfaceSelectionList(u, enc, bt, "SYSTEM", 1, "About\n<< BACK");
    if (sel == 1) showAbout(u, enc, bt);
    // else return
}

static void openMainMenu(u8g2_t* u, Encoder* enc, PushButton* bt, mdaEPiano* ep, RefaceCpChain* fx)
{
    (void)fx;
    uint8_t sel = pico_UserInterfaceSelectionList(u, enc, bt, "MENU", 1, "Presets\nSystem\n<< BACK");
    if (sel == 1)      pico_UserInterfaceProgramSelect(u, enc, bt, ep);
    else if (sel == 2) openSystem(u, enc, bt);
    // else return
}

/* ------------------------------------------------------------------ */
/* Screen enumeration                                                  */
/* ------------------------------------------------------------------ */
enum { SCR_VOLOCT=0, SCR_VOICE, SCR_TREM, SCR_CHO, SCR_DLY, SCR_REV, SCR_VPARAM, SCR_SYSTEM, SCR_COUNT };

/* V.PARAMS whitelist: hides 4 (Modulation), 5 (LFO Rate), 11 (Overdrive)
   -- these are FX-chain duplicates and neutralised in the engine. */
static const uint8_t kVpMap[] = {0,1,2,3,6,7,8,9,10};
static const int kVpCount = 9;

#define LONG_PRESS_MS 500

/* ------------------------------------------------------------------ */
/* Main front-panel interface                                          */
/* ------------------------------------------------------------------ */
void pico_UserInterfaceFrontPanel(u8g2_t* u8g2, Encoder* encSel, PushButton* btSel,
                                  Encoder* encA,  PushButton* btA,
                                  Encoder* encB,  PushButton* btB,
                                  mdaEPiano* ep,  RefaceCpChain* fx)
{
    /* ---- Cache live state ---- */
    int   instr = ep->getCurrentInstrument();
    float drv   = fx->getDrive();
    float twD   = fx->getTremWahDepth();
    float twR   = fx->getTremWahRate();
    float cpD   = fx->getChoPhaDepth();
    float cpS   = fx->getChoPhaSpeed();
    float dlyD  = fx->getDelayDepth();
    float dlyT  = fx->getDelayTime();
    float rev   = fx->getReverbDepth();
    float vol   = fx->getVolume();
    int   twM   = fx->getTremWahMode();
    int   cpM   = fx->getChoPhaMode();
    int   dlyM  = fx->getDelayMode();
    int   oct   = ui_get_octave();
    int   nInstr= ep->getInstrumentCount();
    int     vpIdx = 0;
    float   vpVal= ep->getParameter(kVpMap[0]);
    uint8_t midiCh  = refaceMidi.getRxChannel();
    float   preGain = fx->getPreGain();

    int  screen = SCR_VOLOCT;
    bool selHeld  = false;
    uint32_t selPressT = 0;

    for (;;) { /* outer render loop */

        /* ---------- Render current screen ---------- */
        u8g2_SetFontPosBaseline(u8g2);

        u8g2_FirstPage(u8g2);
        do {
            /* ---- Header ---- */
            u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
            int hasc = u8g2_GetAscent(u8g2);
            int hdesc= u8g2_GetDescent(u8g2);
            char title[24];
            switch (screen) {
                case SCR_VOLOCT: strcpy(title, "VOL / OCT"); break;
                case SCR_VOICE:  strcpy(title, "VOICE");     break;
                case SCR_TREM:
                    switch (twM) {
                        case 1: strcpy(title,"TREM: Tremolo"); break;
                        case 2: strcpy(title,"TREM: Wah");     break;
                        default:strcpy(title,"TREM: Off");    break;
                    }
                    break;
                case SCR_CHO:
                    switch (cpM) {
                        case 1: strcpy(title,"CHO: Chorus"); break;
                        case 2: strcpy(title,"CHO: Phaser"); break;
                        default:strcpy(title,"CHO: Off");    break;
                    }
                    break;
                case SCR_DLY:
                    switch (dlyM) {
                        case 1: strcpy(title,"DLY: Digital"); break;
                        case 2: strcpy(title,"DLY: Analog");  break;
                        default:strcpy(title,"DLY: Off");     break;
                    }
                    break;
                case SCR_REV:    strcpy(title, "REVERB");   break;
                case SCR_VPARAM: strcpy(title, "V.PARAMS"); break;
                case SCR_SYSTEM: strcpy(title, "SYSTEM");   break;
                default:         strcpy(title, "");        break;
            }

            /* inverted header bar */
            u8g2_SetDrawColor(u8g2, 1);
            u8g2_DrawBox(u8g2, 0, 0, 128, hasc - hdesc);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawStr(u8g2, 2, hasc, title);

            /* page indicator right-aligned */
            char page[8];
            snprintf(page, sizeof(page), "%d/%d", screen + 1, SCR_COUNT);
            int pw = u8g2_GetStrWidth(u8g2, page);
            u8g2_DrawStr(u8g2, 126 - pw, hasc, page);
            u8g2_SetDrawColor(u8g2, 1);

            /* separator */
            u8g2_DrawHLine(u8g2, 0, hasc - hdesc, 128);

            /* ---- Body lines A / B ---- */
            u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
            int yA = 32, yB = 48;
            char lineA[32], lineB[32];

            switch (screen) {
                case SCR_VOLOCT:
                    snprintf(lineA, sizeof(lineA), "Vol  %2d", pct(vol));
                    snprintf(lineB, sizeof(lineB), "Oct  %+d", oct);
                    break;
                case SCR_VOICE:
                    snprintf(lineA, sizeof(lineA), "Type %s", ep->getInstrumentName(instr));
                    snprintf(lineB, sizeof(lineB), "Drv  %2d", pct(drv));
                    break;
                case SCR_TREM:
                    snprintf(lineA, sizeof(lineA), "Depth %2d", pct(twD));
                    snprintf(lineB, sizeof(lineB), "Rate  %2d", pct(twR));
                    break;
                case SCR_CHO:
                    snprintf(lineA, sizeof(lineA), "Depth %2d", pct(cpD));
                    snprintf(lineB, sizeof(lineB), "Speed %2d", pct(cpS));
                    break;
                case SCR_DLY:
                    snprintf(lineA, sizeof(lineA), "Depth %2d", pct(dlyD));
                    snprintf(lineB, sizeof(lineB), "Time  %2d", pct(dlyT));
                    break;
                case SCR_REV:
                    snprintf(lineA, sizeof(lineA), "Reverb %2d", pct(rev));
                    snprintf(lineB, sizeof(lineB), "%s", "");
                    break;
                case SCR_VPARAM: {
                    char nm[32];
                    ep->getParameterName(kVpMap[vpIdx], nm);
                    snprintf(lineA, sizeof(lineA), "%s", nm);
                    snprintf(lineB, sizeof(lineB), "Val  %2d", pct(vpVal));
                    break;
                }
                case SCR_SYSTEM: {
                    char chStr[4];
                    if (midiCh == RefaceMidi::RX_CH_ALL) {
                        strcpy(chStr, "All");
                    } else {
                        snprintf(chStr, sizeof(chStr), "%d", midiCh + 1);
                    }
                    snprintf(lineA, sizeof(lineA), "MIDI Ch %s", chStr);
                    snprintf(lineB, sizeof(lineB), "PreGain %2d", pct(preGain));
                    break;
                }
                default:
                    lineA[0] = lineB[0] = 0;
                    break;
            }

            u8g2_DrawStr(u8g2, 4, yA, lineA);
            if (screen != SCR_REV)  /* REV has only one value */
                u8g2_DrawStr(u8g2, 4, yB, lineB);

            /* ---- Hint line ---- */
            u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
            const char* hint;
            if (screen == SCR_TREM || screen == SCR_CHO || screen == SCR_DLY)
                hint = "Sel:Mode  A/B:edit";
            else
                hint = "Sel:Page  A/B:edit";
            u8g2_DrawStr(u8g2, 4, 62, hint);

        } while (u8g2_NextPage(u8g2));

        /* ---------- Inner event loop ---------- */
        for (;;) {
            ui_poll_usb();
            uint32_t now = to_ms_since_boot(get_absolute_time());

            /* ----- External change poll: refresh stale cached locals ----- */
            bool extChanged = false;
            switch (screen) {
                case SCR_VOLOCT: {
                    auto v = fx->getVolume();
                    if (v != vol) { vol = v; extChanged = true; }
                } break;
                case SCR_VOICE: {
                    auto v = ep->getCurrentInstrument();
                    auto d = fx->getDrive();
                    if (v != instr) { instr = v; extChanged = true; }
                    if (d != drv)   { drv = d; extChanged = true; }
                } break;
                case SCR_TREM: {
                    auto m = fx->getTremWahMode();
                    auto d = fx->getTremWahDepth();
                    auto r = fx->getTremWahRate();
                    if (m != twM) { twM = m; extChanged = true; }
                    if (d != twD) { twD = d; extChanged = true; }
                    if (r != twR) { twR = r; extChanged = true; }
                } break;
                case SCR_CHO: {
                    auto m = fx->getChoPhaMode();
                    auto d = fx->getChoPhaDepth();
                    auto s = fx->getChoPhaSpeed();
                    if (m != cpM) { cpM = m; extChanged = true; }
                    if (d != cpD) { cpD = d; extChanged = true; }
                    if (s != cpS) { cpS = s; extChanged = true; }
                } break;
                case SCR_DLY: {
                    auto m = fx->getDelayMode();
                    auto d = fx->getDelayDepth();
                    auto t = fx->getDelayTime();
                    if (m != dlyM) { dlyM = m; extChanged = true; }
                    if (d != dlyD) { dlyD = d; extChanged = true; }
                    if (t != dlyT) { dlyT = t; extChanged = true; }
                } break;
                case SCR_REV: {
                    auto v = fx->getReverbDepth();
                    if (v != rev) { rev = v; extChanged = true; }
                } break;
                case SCR_VPARAM: {
                    auto v = ep->getParameter(kVpMap[vpIdx]);
                    if (v != vpVal) { vpVal = v; extChanged = true; }
                } break;
                case SCR_SYSTEM: {
                    auto v = refaceMidi.getRxChannel();
                    if (v != midiCh) { midiCh = v; extChanged = true; }
                } break;
                default: break;
            }
            if (extChanged) break;

            /* ----- Selector button (short / long press) ----- */
            bool selState = btSel->ReadButton();
            if (selState == PushButton::PRESSED) {
                if (!selHeld) { selHeld = true; selPressT = now; }
                /* keep waiting while held */
            } else { /* RELEASED */
                if (selHeld) {
                    uint32_t dur = now - selPressT;
                    selHeld = false;
                    if (dur >= LONG_PRESS_MS) {
                        /* long press -> menu */
                        openMainMenu(u8g2, encSel, btSel, ep, fx);
                        instr = ep->getCurrentInstrument();
                        break; /* re-render */
                    } else {
                        /* short press -> cycle mode on mode-screens */
                        if (screen == SCR_TREM) {
                            twM = (twM + 1) % 3;
                            fp_send_fx_mode(FXM_TW_MODE, (uint8_t)twM);
                        } else if (screen == SCR_CHO) {
                            cpM = (cpM + 1) % 3;
                            fp_send_fx_mode(FXM_CP_MODE, (uint8_t)cpM);
                        } else if (screen == SCR_DLY) {
                            dlyM = (dlyM + 1) % 3;
                            fp_send_fx_mode(FXM_DLY_MODE, (uint8_t)dlyM);
                        }
                        break; /* re-render */
                    }
                }
            }

            /* ----- Selector encoder delta : change page ----- */
            int32_t ds = encSel->delta();
            if (ds != 0) {
                screen = (screen + (ds > 0 ? 1 : -1) + SCR_COUNT) % SCR_COUNT;
                if (screen == SCR_VPARAM) {
                    vpVal = ep->getParameter(kVpMap[vpIdx]);
                }
                break;
            }

            /* ----- Param A button : reset A default ----- */
            if (btA->ReadButton() == PushButton::PRESSED) {
                ui_wait_button_release(btA);
                switch (screen) {
                    case SCR_VOLOCT:
                        vol = 0.9f; fp_send_fx_param(FX_VOLUME, vol);
                        break;
                    case SCR_VOICE:
                        instr = 0; fp_send_instrument((uint8_t)instr);
                        break;
                    case SCR_TREM:
                        twD = 0.0f; fp_send_fx_param(FX_TW_DEPTH, twD);
                        break;
                    case SCR_CHO:
                        cpD = 0.4f; fp_send_fx_param(FX_CP_DEPTH, cpD);
                        break;
                    case SCR_DLY:
                        dlyD = 0.0f; fp_send_fx_param(FX_DLY_DEPTH, dlyD);
                        break;
                    case SCR_REV:
                        rev = 0.25f; fp_send_fx_param(FX_REVERB, rev);
                        break;
                    case SCR_VPARAM:
                        /* no A reset */
                        break;
                    case SCR_SYSTEM:
                        midiCh = RefaceMidi::RX_CH_ALL;
                        refaceMidi.setRxChannel(midiCh);
                        break;
                }
                break; /* re-render */
            }

            /* ----- Param B button : reset B default ----- */
            if (btB->ReadButton() == PushButton::PRESSED) {
                ui_wait_button_release(btB);
                switch (screen) {
                    case SCR_VOLOCT:
                        oct = 0; ui_set_octave(oct);
                        break;
                    case SCR_VOICE:
                        drv = 0.15f; fp_send_fx_param(FX_DRIVE, drv);
                        break;
                    case SCR_TREM:
                        twR = 0.0f; fp_send_fx_param(FX_TW_RATE, twR);
                        break;
                    case SCR_CHO:
                        cpS = 0.3f; fp_send_fx_param(FX_CP_SPEED, cpS);
                        break;
                    case SCR_DLY:
                        dlyT = 0.0f; fp_send_fx_param(FX_DLY_TIME, dlyT);
                        break;
                    case SCR_REV:
                        /* no B */
                        break;
                    case SCR_VPARAM:
                        vpVal = 0.5f; ipc_send_voice_param(kVpMap[vpIdx], vpVal);
                        break;
                    case SCR_SYSTEM:
                        preGain = 1.0f;
                        fp_send_fx_param(FX_PRE_GAIN, preGain);
                        break;
                }
                break; /* re-render */
            }

            /* ----- Param A encoder delta ----- */
            int32_t dA = encA->delta();
            if (dA != 0) {
                switch (screen) {
                    case SCR_VOLOCT:
                        vol = clamp01f(vol + dA * 0.02f);
                        fp_send_fx_param(FX_VOLUME, vol);
                        break;
                    case SCR_VOICE:
                        instr = (instr + (dA > 0 ? 1 : -1) + nInstr) % nInstr;
                        fp_send_instrument((uint8_t)instr);
                        break;
                    case SCR_TREM:
                        twD = clamp01f(twD + dA * 0.02f);
                        fp_send_fx_param(FX_TW_DEPTH, twD);
                        break;
                    case SCR_CHO:
                        cpD = clamp01f(cpD + dA * 0.02f);
                        fp_send_fx_param(FX_CP_DEPTH, cpD);
                        break;
                    case SCR_DLY:
                        dlyD = clamp01f(dlyD + dA * 0.02f);
                        fp_send_fx_param(FX_DLY_DEPTH, dlyD);
                        break;
                    case SCR_REV:
                        rev = clamp01f(rev + dA * 0.02f);
                        fp_send_fx_param(FX_REVERB, rev);
                        break;
                    case SCR_VPARAM: {
                        vpIdx = (vpIdx + (dA > 0 ? 1 : -1) + kVpCount) % kVpCount;
                        vpVal = ep->getParameter(kVpMap[vpIdx]);
                        break;
                    }
                    case SCR_SYSTEM: {
                        int ch = (int)midiCh + (dA > 0 ? 1 : -1);
                        if (ch < 0) ch = (int)RefaceMidi::RX_CH_ALL;
                        if (ch > (int)RefaceMidi::RX_CH_ALL) ch = 0;
                        midiCh = (uint8_t)ch;
                        refaceMidi.setRxChannel(midiCh);
                        break;
                    }
                }
                break; /* re-render */
            }

            /* ----- Param B encoder delta ----- */
            int32_t dB = encB->delta();
            if (dB != 0) {
                switch (screen) {
                    case SCR_VOLOCT:
                        oct += (dB > 0 ? 1 : -1);
                        if (oct < -2) oct = -2;
                        if (oct >  2) oct =  2;
                        ui_set_octave(oct);
                        break;
                    case SCR_VOICE:
                        drv = clamp01f(drv + dB * 0.02f);
                        fp_send_fx_param(FX_DRIVE, drv);
                        break;
                    case SCR_TREM:
                        twR = clamp01f(twR + dB * 0.02f);
                        fp_send_fx_param(FX_TW_RATE, twR);
                        break;
                    case SCR_CHO:
                        cpS = clamp01f(cpS + dB * 0.02f);
                        fp_send_fx_param(FX_CP_SPEED, cpS);
                        break;
                    case SCR_DLY:
                        dlyT = clamp01f(dlyT + dB * 0.02f);
                        fp_send_fx_param(FX_DLY_TIME, dlyT);
                        break;
                    case SCR_REV:
                        /* unused */
                        break;
                    case SCR_VPARAM:
                        vpVal = clamp01f(vpVal + dB * 0.02f);
                        ipc_send_voice_param(kVpMap[vpIdx], vpVal);
                        break;
                    case SCR_SYSTEM:
                        preGain = clamp01f(preGain + dB * 0.02f);
                        fp_send_fx_param(FX_PRE_GAIN, preGain);
                        break;
                }
                break; /* re-render */
            }

            /* nothing happened, loop again */
        } /* inner event loop */
    } /* outer render loop */
}
