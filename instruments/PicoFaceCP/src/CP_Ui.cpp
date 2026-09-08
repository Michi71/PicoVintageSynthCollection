// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// CP_Ui.cpp - front panel and menu tree of PicoFaceCP, see CP_Ui.h.

#include "CP_Ui.h"

#include <cstdio>
#include <cstring>

#include "u8g2.h"

#include "picoface/ui_kit.h"

#include "ipc.h"
#include "mdaEPiano.h"
#include "midi_reface.h"
#include "presets.h"
#include "reface_cp_chain.h"

// Octave transpose lives in CP_Instrument.cpp, next to the MIDI dispatch that
// applies it. Same extern "C" pair that settings.cpp and midi_reface.cpp use.
extern "C" void ui_set_octave(int oct);
extern "C" int  ui_get_octave(void);

using picoface::ui::Button;
using picoface::ui::Display;
using picoface::ui::Encoder;
using picoface::ui::InputState;

namespace {

const char* const kMenuEntries[]   = {"Presets", "System", "<< BACK"};
const char* const kSystemEntries[] = {"About", "<< BACK"};

// A menu list left alone this long falls back to the panel, as the blocking
// selection list did.
constexpr uint32_t kMenuIdleMs = 5000;

constexpr uint32_t kMinRedrawMs = 33;
constexpr uint32_t kMaxRedrawMs = 500;

// V.PARAMS whitelist: hides 4 (Modulation), 5 (LFO Rate), 11 (Overdrive) -
// these are FX-chain duplicates and neutralised in the engine.
const uint8_t kVpMap[] = {0, 1, 2, 3, 6, 7, 8, 9, 10};
constexpr int kVpCount = 9;

// One encoder detent per 2 % of a normalised parameter, as before.
constexpr float kStep = 0.02f;

inline float clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

int pct(float v)
{
    int x = (int)(v * 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 100) x = 100;
    return x;
}

#ifdef PICOFACE_INSTRUMENT_NAME
constexpr const char* kAboutName = PICOFACE_INSTRUMENT_NAME;
#else
constexpr const char* kAboutName = "PicoFaceCP";
#endif

#ifdef PICOFACE_VERSION
constexpr const char* kAboutVersion = PICOFACE_VERSION;
#else
constexpr const char* kAboutVersion = "0.1";
#endif

} // namespace

CP_Ui::CP_Ui(mdaEPiano& ep, RefaceCpChain& fx, RefaceMidi& midi)
    : ep_(ep), fx_(fx), midi_(midi) {}

// ---------------------------------------------------------------------------
// panel edits: ring first, then the MIDI mirror - exactly as the blocking
// panel did through its fp_send_* helpers
// ---------------------------------------------------------------------------

void CP_Ui::refresh()
{
    instr_   = ep_.getCurrentInstrument();
    nInstr_  = ep_.getInstrumentCount();
    if (nInstr_ < 1) nInstr_ = 1;
    drv_     = fx_.getDrive();
    twD_     = fx_.getTremWahDepth();
    twR_     = fx_.getTremWahRate();
    cpD_     = fx_.getChoPhaDepth();
    cpS_     = fx_.getChoPhaSpeed();
    dlyD_    = fx_.getDelayDepth();
    dlyT_    = fx_.getDelayTime();
    rev_     = fx_.getReverbDepth();
    vol_     = fx_.getVolume();
    preGain_ = fx_.getPreGain();
    twM_     = fx_.getTremWahMode();
    cpM_     = fx_.getChoPhaMode();
    dlyM_    = fx_.getDelayMode();
    oct_     = ui_get_octave();
    vpVal_   = ep_.getParameter(kVpMap[vpIdx_]);
    midiCh_  = midi_.getRxChannel();
    presetIdx_ = preset_get_current();
}

void CP_Ui::go(Screen next, uint32_t nowMs)
{
    screen_       = next;
    dirty_        = true;
    lastInputMs_  = nowMs;
    // Whatever release is still pending belongs to the screen we are leaving.
    selConsumed_  = true;

    if (next == Screen::Menu) {
        list_.open(kMenuEntries, 3);
    } else if (next == Screen::MenuSystem) {
        list_.open(kSystemEntries, 2, sysCursor_);
    } else if (next == Screen::Panel || next == Screen::Preset) {
        // A preset or an instrument change may have moved everything.
        refresh();
    }
}

void CP_Ui::cycleMode()
{
    if (page_ == PG_TREM) {
        twM_ = (twM_ + 1) % 3;
        ipc_send_fx_mode(FXM_TW_MODE, (uint8_t) twM_);
        midi_.txFxMode(FXM_TW_MODE, twM_);
    } else if (page_ == PG_CHO) {
        cpM_ = (cpM_ + 1) % 3;
        ipc_send_fx_mode(FXM_CP_MODE, (uint8_t) cpM_);
        midi_.txFxMode(FXM_CP_MODE, cpM_);
    } else if (page_ == PG_DLY) {
        dlyM_ = (dlyM_ + 1) % 3;
        ipc_send_fx_mode(FXM_DLY_MODE, (uint8_t) dlyM_);
        midi_.txFxMode(FXM_DLY_MODE, dlyM_);
    }
}

void CP_Ui::resetA()
{
    switch (page_) {
    case PG_VOLOCT: vol_ = 0.9f;  ipc_send_fx_param(FX_VOLUME, vol_);   midi_.txFxParam(FX_VOLUME, vol_);   break;
    case PG_VOICE:  instr_ = 0;   ipc_send_instrument((uint8_t) instr_); midi_.txInstrument(instr_);         break;
    case PG_TREM:   twD_ = 0.0f;  ipc_send_fx_param(FX_TW_DEPTH, twD_); midi_.txFxParam(FX_TW_DEPTH, twD_); break;
    case PG_CHO:    cpD_ = 0.4f;  ipc_send_fx_param(FX_CP_DEPTH, cpD_); midi_.txFxParam(FX_CP_DEPTH, cpD_); break;
    case PG_DLY:    dlyD_ = 0.0f; ipc_send_fx_param(FX_DLY_DEPTH, dlyD_); midi_.txFxParam(FX_DLY_DEPTH, dlyD_); break;
    case PG_REV:    rev_ = 0.25f; ipc_send_fx_param(FX_REVERB, rev_);   midi_.txFxParam(FX_REVERB, rev_);   break;
    case PG_VPARAM: break;   // no A reset
    case PG_SYSTEM: midiCh_ = RefaceMidi::RX_CH_ALL; midi_.setRxChannel(midiCh_); break;
    default: break;
    }
}

void CP_Ui::resetB()
{
    switch (page_) {
    case PG_VOLOCT: oct_ = 0; ui_set_octave(oct_); break;
    case PG_VOICE:  drv_ = 0.15f; ipc_send_fx_param(FX_DRIVE, drv_);    midi_.txFxParam(FX_DRIVE, drv_);    break;
    case PG_TREM:   twR_ = 0.0f;  ipc_send_fx_param(FX_TW_RATE, twR_);  midi_.txFxParam(FX_TW_RATE, twR_);  break;
    case PG_CHO:    cpS_ = 0.3f;  ipc_send_fx_param(FX_CP_SPEED, cpS_); midi_.txFxParam(FX_CP_SPEED, cpS_); break;
    case PG_DLY:    dlyT_ = 0.0f; ipc_send_fx_param(FX_DLY_TIME, dlyT_); midi_.txFxParam(FX_DLY_TIME, dlyT_); break;
    case PG_REV:    break;   // no B
    case PG_VPARAM: vpVal_ = 0.5f; ipc_send_voice_param(kVpMap[vpIdx_], vpVal_); break;
    case PG_SYSTEM: preGain_ = 1.0f; ipc_send_fx_param(FX_PRE_GAIN, preGain_); midi_.txFxParam(FX_PRE_GAIN, preGain_); break;
    default: break;
    }
}

void CP_Ui::editA(int8_t delta)
{
    switch (page_) {
    case PG_VOLOCT:
        vol_ = clamp01f(vol_ + delta * kStep);
        ipc_send_fx_param(FX_VOLUME, vol_);
        midi_.txFxParam(FX_VOLUME, vol_);
        break;
    case PG_VOICE:
        instr_ = (instr_ + (delta > 0 ? 1 : -1) + nInstr_) % nInstr_;
        ipc_send_instrument((uint8_t) instr_);
        midi_.txInstrument(instr_);
        break;
    case PG_TREM:
        twD_ = clamp01f(twD_ + delta * kStep);
        ipc_send_fx_param(FX_TW_DEPTH, twD_);
        midi_.txFxParam(FX_TW_DEPTH, twD_);
        break;
    case PG_CHO:
        cpD_ = clamp01f(cpD_ + delta * kStep);
        ipc_send_fx_param(FX_CP_DEPTH, cpD_);
        midi_.txFxParam(FX_CP_DEPTH, cpD_);
        break;
    case PG_DLY:
        dlyD_ = clamp01f(dlyD_ + delta * kStep);
        ipc_send_fx_param(FX_DLY_DEPTH, dlyD_);
        midi_.txFxParam(FX_DLY_DEPTH, dlyD_);
        break;
    case PG_REV:
        rev_ = clamp01f(rev_ + delta * kStep);
        ipc_send_fx_param(FX_REVERB, rev_);
        midi_.txFxParam(FX_REVERB, rev_);
        break;
    case PG_VPARAM:
        // A selects the parameter, B edits it.
        vpIdx_ = (vpIdx_ + (delta > 0 ? 1 : -1) + kVpCount) % kVpCount;
        vpVal_ = ep_.getParameter(kVpMap[vpIdx_]);
        break;
    case PG_SYSTEM: {
        int ch = (int) midiCh_ + (delta > 0 ? 1 : -1);
        if (ch < 0) ch = (int) RefaceMidi::RX_CH_ALL;
        if (ch > (int) RefaceMidi::RX_CH_ALL) ch = 0;
        midiCh_ = (uint8_t) ch;
        midi_.setRxChannel(midiCh_);
        break;
    }
    default: break;
    }
}

void CP_Ui::editB(int8_t delta)
{
    switch (page_) {
    case PG_VOLOCT:
        oct_ += (delta > 0 ? 1 : -1);
        if (oct_ < -2) oct_ = -2;
        if (oct_ >  2) oct_ =  2;
        ui_set_octave(oct_);
        break;
    case PG_VOICE:
        drv_ = clamp01f(drv_ + delta * kStep);
        ipc_send_fx_param(FX_DRIVE, drv_);
        midi_.txFxParam(FX_DRIVE, drv_);
        break;
    case PG_TREM:
        twR_ = clamp01f(twR_ + delta * kStep);
        ipc_send_fx_param(FX_TW_RATE, twR_);
        midi_.txFxParam(FX_TW_RATE, twR_);
        break;
    case PG_CHO:
        cpS_ = clamp01f(cpS_ + delta * kStep);
        ipc_send_fx_param(FX_CP_SPEED, cpS_);
        midi_.txFxParam(FX_CP_SPEED, cpS_);
        break;
    case PG_DLY:
        dlyT_ = clamp01f(dlyT_ + delta * kStep);
        ipc_send_fx_param(FX_DLY_TIME, dlyT_);
        midi_.txFxParam(FX_DLY_TIME, dlyT_);
        break;
    case PG_REV:
        break;   // unused
    case PG_VPARAM:
        vpVal_ = clamp01f(vpVal_ + delta * kStep);
        ipc_send_voice_param(kVpMap[vpIdx_], vpVal_);
        break;
    case PG_SYSTEM:
        preGain_ = clamp01f(preGain_ + delta * kStep);
        ipc_send_fx_param(FX_PRE_GAIN, preGain_);
        midi_.txFxParam(FX_PRE_GAIN, preGain_);
        break;
    default: break;
    }
}

void CP_Ui::tickPanel(const InputState& in, bool selReleased)
{
    if (in.longPress(Button::Sel)) {
        selConsumed_ = true;
        go(Screen::Menu, in.nowMs);
        return;
    }

    // Short press acts on release, so a press that turns out to be long does
    // not also cycle the mode.
    if (selReleased && !selConsumed_) {
        cycleMode();
    }

    const int8_t dSel = in.delta(Encoder::Sel);
    if (dSel != 0) {
        int p = (int) page_ + (dSel > 0 ? 1 : -1);
        p = (p + PG_COUNT) % PG_COUNT;
        page_ = (Page) p;
        if (page_ == PG_VPARAM) vpVal_ = ep_.getParameter(kVpMap[vpIdx_]);
    }

    if (in.pressed(Button::ParamA)) resetA();
    if (in.pressed(Button::ParamB)) resetB();

    const int8_t dA = in.delta(Encoder::ParamA);
    const int8_t dB = in.delta(Encoder::ParamB);
    if (dA != 0) editA(dA);
    if (dB != 0) editB(dB);

    // Pick up what changed behind our back - MIDI, SysEx, a preset. The
    // blocking panel polled the same getters between two renders.
    const float vol = fx_.getVolume();
    const float drv = fx_.getDrive();
    const int   ins = ep_.getCurrentInstrument();
    const float twD = fx_.getTremWahDepth(), twR = fx_.getTremWahRate();
    const float cpD = fx_.getChoPhaDepth(), cpS = fx_.getChoPhaSpeed();
    const float dlD = fx_.getDelayDepth(),  dlT = fx_.getDelayTime();
    const float rev = fx_.getReverbDepth(), pre = fx_.getPreGain();
    const int   twM = fx_.getTremWahMode(), cpM = fx_.getChoPhaMode(), dlM = fx_.getDelayMode();
    const float vpV = ep_.getParameter(kVpMap[vpIdx_]);
    const uint8_t ch = midi_.getRxChannel();

    // Only adopt a value the panel is not editing this very tick; otherwise a
    // still-unapplied edit would be pulled back to the engine's old value.
    if (dA == 0 && dB == 0 && !in.pressed(Button::ParamA) && !in.pressed(Button::ParamB)) {
        if (vol != vol_ || drv != drv_ || ins != instr_ || twD != twD_ || twR != twR_ ||
            cpD != cpD_ || cpS != cpS_ || dlD != dlyD_ || dlT != dlyT_ || rev != rev_ ||
            pre != preGain_ || twM != twM_ || cpM != cpM_ || dlM != dlyM_ ||
            vpV != vpVal_ || ch != midiCh_) {
            vol_ = vol; drv_ = drv; instr_ = ins;
            twD_ = twD; twR_ = twR; cpD_ = cpD; cpS_ = cpS;
            dlyD_ = dlD; dlyT_ = dlT; rev_ = rev; preGain_ = pre;
            twM_ = twM; cpM_ = cpM; dlyM_ = dlM;
            vpVal_ = vpV; midiCh_ = ch;
            dirty_ = true;
        }
    }
}

void CP_Ui::tick(Display& d, const InputState& in)
{
    const bool anyInput = in.delta(Encoder::Sel) || in.delta(Encoder::ParamA) ||
                          in.delta(Encoder::ParamB) || in.pressed(Button::Sel) ||
                          in.pressed(Button::ParamA) || in.pressed(Button::ParamB);
    if (anyInput) {
        dirty_       = true;
        lastInputMs_ = in.nowMs;
    }

    const bool selDown     = in.down(Button::Sel);
    const bool selReleased = (selWasDown_ && !selDown);
    selWasDown_ = selDown;
    if (selReleased) {
        dirty_       = true;
        lastInputMs_ = in.nowMs;
    }

    switch (screen_) {
    case Screen::Panel:
        tickPanel(in, selReleased);
        break;

    case Screen::Menu: {
        const int sel = list_.update(in);
        if (sel == 0)      go(Screen::Preset, in.nowMs);
        else if (sel == 1) go(Screen::MenuSystem, in.nowMs);
        else if (sel == 2) go(Screen::Panel, in.nowMs);
        break;
    }

    case Screen::MenuSystem: {
        const int sel = list_.update(in);
        if (sel == 0) { sysCursor_ = 0; go(Screen::About, in.nowMs); }
        else if (sel == 1) go(Screen::Menu, in.nowMs);
        break;
    }

    case Screen::About:
        if (in.pressed(Button::Sel) || in.pressed(Button::ParamA) || in.pressed(Button::ParamB)) {
            go(Screen::MenuSystem, in.nowMs);
        }
        break;

    case Screen::Preset: {
        const int8_t dSel = in.delta(Encoder::Sel);
        if (dSel != 0) {
            int v = (int) presetIdx_ + (dSel > 0 ? 1 : -1);
            if (v < 0) v = CP_NPRESETS - 1;
            if (v > CP_NPRESETS - 1) v = 0;
            presetIdx_ = (uint8_t) v;
            preset_set_current(presetIdx_);
            ipc_send_program(presetIdx_);
            midi_.txProgram(presetIdx_);
        }
        if (in.pressed(Button::Sel)) go(Screen::Menu, in.nowMs);
        break;
    }
    }

    // Never strand the user in a list. About and Preset are left out: reading
    // the one and auditioning the other both take longer than five seconds.
    if ((screen_ == Screen::Menu || screen_ == Screen::MenuSystem) &&
        (in.nowMs - lastInputMs_) > kMenuIdleMs) {
        go(Screen::Panel, in.nowMs);
    }

    if (!selDown) {
        selConsumed_ = false;
    }

    if ((dirty_ && (in.nowMs - lastDrawMs_) >= kMinRedrawMs) ||
        (in.nowMs - lastDrawMs_) >= kMaxRedrawMs) {
        draw(d);
        dirty_      = false;
        lastDrawMs_ = in.nowMs;
    }
}

void CP_Ui::drawNow(Display& d)
{
    // First frame: the cache is still empty at construction time, because the
    // engine and FX defaults are only set in init().
    refresh();
    draw(d);
    dirty_ = false;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

void CP_Ui::draw(Display& d)
{
    switch (screen_) {
    case Screen::Panel:      drawPanel(d);              break;
    case Screen::Menu:       list_.draw(d, "MENU");     break;
    case Screen::MenuSystem: list_.draw(d, "SYSTEM");   break;
    case Screen::About:      drawAbout(d);              break;
    case Screen::Preset:     drawPreset(d);             break;
    }

    // Arms the incremental push; the core loop sends the buffer out in half
    // tile rows.
    d.flush();
}

void CP_Ui::drawPanel(Display& d)
{
    namespace kit = picoface::ui::kit;
    char title[24], va[32], vb[32];

    d.clear();

    switch (page_) {
    case PG_VOLOCT: strcpy(title, "VOL / OCT"); break;
    case PG_VOICE:  strcpy(title, "VOICE");     break;
    case PG_TREM:
        strcpy(title, twM_ == 1 ? "TREM: Tremolo" : (twM_ == 2 ? "TREM: Wah" : "TREM: Off"));
        break;
    case PG_CHO:
        strcpy(title, cpM_ == 1 ? "CHO: Chorus" : (cpM_ == 2 ? "CHO: Phaser" : "CHO: Off"));
        break;
    case PG_DLY:
        strcpy(title, dlyM_ == 1 ? "DLY: Digital" : (dlyM_ == 2 ? "DLY: Analog" : "DLY: Off"));
        break;
    case PG_REV:    strcpy(title, "REVERB");   break;
    case PG_VPARAM: strcpy(title, "V.PARAMS"); break;
    case PG_SYSTEM: strcpy(title, "SYSTEM");   break;
    default:        title[0] = 0;              break;
    }

    kit::header(d, title, (int) page_, (int) PG_COUNT);

    // The reface CP prints its panel in percent, and so does this: the values
    // are normalised floats, the knob shows where they sit, the number is what
    // the original's display would have said.
    switch (page_) {
    case PG_VOLOCT:
        snprintf(va, sizeof(va), "%d", pct(vol_));
        snprintf(vb, sizeof(vb), "%+d", oct_);
        // The octave switch has five detents, not a sweep.
        kit::panelDuo(d, { "Vol", va, vol_ }, { "Oct", vb });
        break;
    case PG_VOICE:
        snprintf(va, sizeof(va), "%s", ep_.getInstrumentName(instr_));
        snprintf(vb, sizeof(vb), "%d", pct(drv_));
        kit::panelDuo(d, { "Type", va }, { "Drive", vb, drv_ });
        break;
    case PG_TREM:
        snprintf(va, sizeof(va), "%d", pct(twD_));
        snprintf(vb, sizeof(vb), "%d", pct(twR_));
        kit::panelDuo(d, { "Depth", va, twD_ }, { "Rate", vb, twR_ });
        break;
    case PG_CHO:
        snprintf(va, sizeof(va), "%d", pct(cpD_));
        snprintf(vb, sizeof(vb), "%d", pct(cpS_));
        kit::panelDuo(d, { "Depth", va, cpD_ }, { "Speed", vb, cpS_ });
        break;
    case PG_DLY:
        snprintf(va, sizeof(va), "%d", pct(dlyD_));
        snprintf(vb, sizeof(vb), "%d", pct(dlyT_));
        kit::panelDuo(d, { "Depth", va, dlyD_ }, { "Time", vb, dlyT_ });
        break;
    case PG_REV:
        snprintf(va, sizeof(va), "%d", pct(rev_));
        kit::panelDuo(d, { "Reverb", va, rev_ }, {});
        break;
    case PG_VPARAM: {
        // The name comes out of the engine and can be long ("Envelope Decay"),
        // so it is the page title rather than a label in half a cell.
        char nm[32];
        ep_.getParameterName(kVpMap[vpIdx_], nm);
        snprintf(vb, sizeof(vb), "%d", pct(vpVal_));
        kit::panelDuo(d, { "Param", nm }, { "Value", vb, vpVal_ });
        break;
    }
    case PG_SYSTEM:
        if (midiCh_ == RefaceMidi::RX_CH_ALL) strcpy(va, "All");
        else snprintf(va, sizeof(va), "%d", midiCh_ + 1);
        snprintf(vb, sizeof(vb), "%d", pct(preGain_));
        kit::panelDuo(d, { "MIDI Ch", va }, { "PreGain", vb, preGain_ });
        break;
    default:
        break;
    }

    kit::footer(d, (page_ == PG_TREM || page_ == PG_CHO || page_ == PG_DLY)
                       ? "Sel:Mode  A/B:edit"
                       : "Sel:Page  A/B:edit");
}

void CP_Ui::drawPreset(Display& d) const
{
    u8g2_t* u = d.raw();
    char buf[24];

    d.clear();
    u8g2_SetFontDirection(u, 0);
    u8g2_SetFontPosBaseline(u);
    u8g2_SetDrawColor(u, 1);

    u8g2_SetFont(u, u8g2_font_8x13B_tf);
    u8g2_DrawStr(u, (u8g2_uint_t)((Display::kWidth - u8g2_GetStrWidth(u, "PRESET")) / 2), 10, "PRESET");
    u8g2_DrawHLine(u, 0, 12, Display::kWidth);

    snprintf(buf, sizeof(buf), "%s", cpPresets[presetIdx_].name);
    u8g2_DrawStr(u, (u8g2_uint_t)((Display::kWidth - u8g2_GetStrWidth(u, buf)) / 2), 60, buf);

    snprintf(buf, sizeof(buf), "P%03u", (unsigned) presetIdx_);
    u8g2_SetFont(u, u8g2_font_fub25_tf);
    u8g2_DrawStr(u, 0, 44, buf);
}

void CP_Ui::drawAbout(Display& d) const
{
    // Only ever visible when the ring overflowed between two rendered blocks -
    // otherwise the hint has the line.
    char buf[24];
    const char* hint = "Press any button";
    if (cp_ipc_dropped != 0) {
        snprintf(buf, sizeof(buf), "IPC dropped %lu", (unsigned long) cp_ipc_dropped);
        hint = buf;
    }
    picoface::ui::kit::about(d, kAboutName, kAboutVersion, hint);
}
