// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// SM_Instrument.cpp - adapter binding the ARP/Eminent Solina String Ensemble
// classes to picoface::Instrument. Same shape as MD_Instrument.cpp.

#include <cstdio>
#include <cstring>

#include "picoface/instrument.h"

#include "SM_Synth_Bridge.h"
#include "SM_Midi.h"
#include "SM_Controller.h"
#include "SM_Display.h"
#include "sm_settings.h"
#include "sm_ipc.h"
#include "solina/solina.h"
#include "audio_i2s.h" // g_i2s_underrun_count for the diagnostics footer

namespace {

class SMInstrument final : public picoface::Instrument {
public:
    // SM_Controller takes SM_Midi by reference
    SMInstrument() : controller_(midi_) {}

    const char* name() const override { return "PicoFaceSM"; }

    void init() override {
        bridge_.init();
        midi_.init();
        // unlike MD the Solina front end has no UI sink - the controller is
        // not mirrored from MIDI
    }

    uint32_t sampleRate() const override { return bridge_.currentSampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        // drain the IPC ring first, so panel and MIDI edits land on a block boundary
        uint32_t pkt;
        while (sm_ipc_pop(&pkt)) applyIpc(pkt);
        bridge_.fill_buffer_i32(out, (int) frames);
    }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(note, vel, ch); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOff(note, vel, ch); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(cc, v, ch); }
    void programChange(uint8_t ch, uint8_t p) override { midi_.onProgramChange(p, ch); }

    // the core hands over -8192..8191, SM_Midi expects the raw 14-bit value
    // 0..16383 with centre 8192
    void pitchBend(uint8_t ch, int16_t bend) override { midi_.onPitchBend((uint16_t) ((int32_t) bend + 8192), ch); }

    // ------------------------------------------------------------------
    // GUI
    // ------------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override { draw(d); }   // first frame

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override
    {
        using picoface::ui::Encoder;
        using picoface::ui::Button;

        // Push button of the first encoder: back to page 1. Changing the page
        // alters no parameter and therefore does not mark the settings dirty.
        if (in.pressed(Button::Sel)) { if (controller_.homePage()) dirty_ = true; }

        const int8_t d1 = in.delta(Encoder::Sel);
        const int8_t d2 = in.delta(Encoder::ParamA);
        const int8_t d3 = in.delta(Encoder::ParamB);
        if (d1) { controller_.onEncoder1(d1); dirty_ = true; }
        if (d2) { controller_.onEncoder2(d2); dirty_ = true; }
        if (d3) { controller_.onEncoder3(d3); dirty_ = true; }

        // Redraw promptly after changes; force a periodic refresh so the
        // footer statistics stay alive even without user input.
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u)
        {
            draw(d);
            dirty_     = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // ------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------

    uint16_t settingsVersion() const override { return SM_SETTINGS_VERSION; }
    size_t   settingsSize()    const override { return sizeof(SmSettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override
    {
        if (size < sizeof(SmSettingsV1)) return;
        SmSettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override
    {
        if (size < sizeof(SmSettingsV1)) return;
        SmSettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
    }

private:

    // Routes one IPC packet to the bridge (called from render(), on a block boundary).
    void applyIpc(uint32_t pkt)
    {
        switch (ipc_type(pkt))
        {
            case IPC_CMD_SM_NOTE_ON:
                bridge_.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
                break;

            case IPC_CMD_SM_NOTE_OFF:
                bridge_.noteOff(ipc_d1(pkt));
                break;

            case IPC_CMD_SM_CC:
                switch (ipc_d1(pkt))
                {
                    case 7:   bridge_.setVolume((uint8_t) ipc_d2(pkt)); break; // channel volume
                    case 64:  bridge_.sustain((uint8_t) ipc_d2(pkt));   break; // sustain pedal
                    case 120: // all sound off
                    case 123: bridge_.allNotesOff(); break;                  // all notes off
                    default:  break;
                }
                break;

            case IPC_CMD_SM_PITCH_BEND:
                bridge_.pitchBend(ipc_d2(pkt));
                break;

            case IPC_CMD_SM_PARAM:
            {
                const uint8_t  id = ipc_d1(pkt);
                const uint16_t v  = ipc_d2(pkt);
                if (id == SM_PARAM_PROGRAM)
                    bridge_.setProgram((int32_t) v);
                else if (id < SOLINA_PARAM_COUNT)
                    bridge_.setParameter(id, (float) v / 1000.0f); // per mille
                break;
            }

            default:
                break;
        }
    }

    // The Solina has only a page view, no list.
    void draw(picoface::ui::Display& d)
    {
        static SmUiModel m;
        char va[20], vb[20];

        snprintf(m.title, sizeof(m.title), "%s", controller_.pageName());
        snprintf(m.page,  sizeof(m.page),  "%d/%d",
                 controller_.currentPage() + 1, controller_.pageCount());

        controller_.paramAText(va, sizeof(va));
        controller_.paramBText(vb, sizeof(vb));
        const char* na = controller_.paramAName();
        const char* nb = controller_.paramBName();

        // Empty label = the value describes itself.
        if (na[0]) snprintf(m.lineA, sizeof(m.lineA), "%s %s", na, va);
        else       snprintf(m.lineA, sizeof(m.lineA), "%s", va);
        if (nb[0]) snprintf(m.lineB, sizeof(m.lineB), "%s %s", nb, vb);
        else       snprintf(m.lineB, sizeof(m.lineB), "%s", vb);

        // Footer: CPU peak, I2S underruns, dropped IPC packets, note-on count.
        snprintf(m.footer, sizeof(m.footer), "P%d U%lu D%lu N%lu",
                 (int) bridge_.cpuLoadPeakPercent(),
                 (unsigned long) g_i2s_underrun_count,
                 (unsigned long) sm_ipc_dropped,
                 (unsigned long) bridge_.noteOnCount());
        bridge_.resetCpuPeak();

        sm_display_page(d.raw(), m);
        d.flush();   // arms the incremental push
    }

    SM_Synth_Bridge bridge_;
    SM_Midi         midi_;
    SM_Controller   controller_;   // declared after midi_: taken by reference in the constructor
    bool            dirty_      = false;
    uint32_t        lastDrawMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(SMInstrument)
