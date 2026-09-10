// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// MD_Instrument.cpp - adapter binding the existing Minimoog Model D classes
// to picoface::Instrument. Reference implementation: the other five
// instruments follow this shape. The adapter owns the objects that
// md_main.cpp used to hold as globals and translates between the picoface
// interface and the existing MD_* APIs.

#include <cstdio>
#include <cstring>

#include "picoface/instrument.h"

#include "MD_Synth_Bridge.h"
#include "MD_Midi.h"
#include "MD_Controller.h"
#include "picoface/ui_kit.h"
#include "md_settings.h"
#include "md_ipc.h"
#include "moog/moog.h"

// g_i2s_underrun_count for the diagnostics footer. Part of the shared Audio
// library this instrument links anyway - no coupling to the core.
#include "audio_i2s.h"

namespace {

class MDInstrument final : public picoface::Instrument {
public:
    // MD_Controller takes MD_Midi by reference
    MDInstrument() : controller_(midi_) {}

    const char* name() const override { return "PicoFaceMD"; }

    void init() override {
        bridge_.init();
        midi_.init();
        // so a value arriving over MIDI updates the display too, not just the engine
        midi_.setUiSink(&controller_);
    }

    uint32_t sampleRate() const override { return bridge_.currentSampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        // drain the IPC ring first - MIDI and panel edits arrive from the same core
        // but outside the producer, so they are applied at block boundaries
        uint32_t pkt;
        while (md_ipc_pop(&pkt)) applyIpc(pkt);
        bridge_.fill_buffer_i32(out, (int) frames);
    }

    // the picoface interface passes the channel first, MD_Midi expects it last
    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(note, vel, ch); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOff(note, vel, ch); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(cc, v, ch); }
    void programChange(uint8_t ch, uint8_t p) override { midi_.onProgramChange(p, ch); }
    // the core hands over -8192..8191, MD_Midi expects the raw 14-bit value
    // 0..16383 with centre 8192
    void pitchBend(uint8_t ch, int16_t bend) override { midi_.onPitchBend((uint16_t)((int32_t) bend + 8192), ch); }

    // ----------------------------------------------------------------
    // GUI
    // ----------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override
    {
        // first frame, so the screen is not blank while the engine settles
        draw(d);
    }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override
    {
        using picoface::ui::Encoder;
        using picoface::ui::Button;

        // the core has already debounced this and only reports the rising edge
        if (in.pressed(Button::Sel))
        {
            if (controller_.onSelectButton())
            {
                dirty_ = true;
            }
        }

        const int8_t d1 = in.delta(Encoder::Sel), d2 = in.delta(Encoder::ParamA), d3 = in.delta(Encoder::ParamB);

        if (d1) { controller_.onEncoder1(d1); dirty_ = true; }
        if (d2) { controller_.onEncoder2(d2); dirty_ = true; }
        if (d3) { controller_.onEncoder3(d3); dirty_ = true; }

        // 50 ms after an edit, otherwise a refresh twice a second for the diagnostics footer
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u)
        {
            draw(d);
            dirty_      = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // ----------------------------------------------------------------
    // Persistence
    // ----------------------------------------------------------------

    uint16_t settingsVersion() const override { return MD_SETTINGS_VERSION; }

    size_t settingsSize() const override { return sizeof(MdSettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override
    {
        if (size < sizeof(MdSettingsV1))
        {
            return;
        }

        // MdSettingsV1 is packed, so a byte copy is safe
        MdSettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override
    {
        if (size < sizeof(MdSettingsV1))
        {
            return;
        }

        // the core has already checked the version; a shorter record is rejected here
        MdSettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
    }

private:
    void applyIpc(uint32_t pkt) {
        switch (ipc_type(pkt)) {
        case IPC_CMD_MD_NOTE_ON:
            bridge_.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_MD_NOTE_OFF:
            bridge_.noteOff(ipc_d1(pkt));
            break;
        case IPC_CMD_MD_CC:
            // only controllers that are not panel parameters get here; the rest arrives as IPC_CMD_MD_PARAM
            bridge_.controlChange(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_MD_PITCH_BEND:
            bridge_.pitchBend(ipc_d2(pkt));
            break;
        case IPC_CMD_MD_PARAM: {
            const uint8_t id = ipc_d1(pkt);
            const uint16_t v  = ipc_d2(pkt);
            if (id == MD_PARAM_PROGRAM) bridge_.setProgram((int32_t) v);
            else if (id < MOOG_PARAM_COUNT) bridge_.setParameter(id, (float) v / 1000.0f);
            break;
        }
        default:
            break;
        }
    }

    void draw(picoface::ui::Display& d) {
        namespace kit = picoface::ui::kit;

        // The developer's numbers went from a strip at the bottom of every page
        // to a page of their own (#161: that strip cost the body nine rows on
        // a screen with none to spare). Formatted here because only this
        // instrument knows what its numbers are; 6x12 holds 21 characters.
        if (controller_.isDiagPage()) {
            char r0[22], r1[22], r2[22], r3[22];
            snprintf(r0, sizeof r0, "CPU peak %d%%", (int) bridge_.cpuLoadPeakPercent());
            snprintf(r1, sizeof r1, "Underruns %lu", (unsigned long) g_i2s_underrun_count);
            snprintf(r2, sizeof r2, "IPC dropped %lu", (unsigned long) md_ipc_dropped);
            snprintf(r3, sizeof r3, "Notes %lu", (unsigned long) bridge_.noteOnCount());
            bridge_.resetCpuPeak();
            const char* rows[4] = { r0, r1, r2, r3 };
            kit::diagnostics(d, controller_.title(), rows, 4);
            d.flush();
            return;
        }


        d.clear();
        kit::header(d, controller_.title(), controller_.pageIndex(),
                    controller_.pageTotal());

        if (controller_.viewKind() == MD_VIEW_LIST) {
            // The kit shows four rows and keeps the cursor one from the top;
            // the window is computed here because only the controller knows
            // how long the list is.
            static char        rows[4][22];
            static const char* ptr[4];
            const int n   = controller_.listCount();
            const int cur = controller_.listCursor();

            int top = cur - 1;
            if (top > n - 4) top = n - 4;
            if (top < 0)     top = 0;

            int shown = 0;
            for (int i = 0; i < 4 && top + i < n; ++i) {
                controller_.listEntry(top + i, rows[i], sizeof(rows[i]));
                ptr[i] = rows[i];
                ++shown;
            }
            kit::list(d, ptr, shown, cur - top);
        } else {
            char va[20], vb[20];
            controller_.paramAText(va, sizeof(va));
            controller_.paramBText(vb, sizeof(vb));

            // An empty name means the value describes itself (the preset does),
            // and the kit prints the value alone in that case.
            kit::panelDuo(d,
                { controller_.paramAName(), va, controller_.paramANorm() },
                { controller_.paramBName(), vb, controller_.paramBNorm() });
        }


        // arms the incremental push; the core loop sends the buffer out in half tile rows
        d.flush();
    }

    MD_Synth_Bridge bridge_;
    MD_Midi         midi_;
    // declared after midi_ because it takes it by reference in the constructor
    MD_Controller   controller_;
    bool            dirty_      = false;
    uint32_t        lastDrawMs_ = 0;
};
} // namespace

PICOFACE_REGISTER_INSTRUMENT(MDInstrument)
