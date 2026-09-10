// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// J6_Instrument.cpp - adapter binding the Roland Juno-6 classes to
// picoface::Instrument. Same shape as MD_Instrument.cpp, plus the user patch
// store.

#include <cstdio>
#include <cstring>

#include "picoface/instrument.h"
#include "J6_Synth_Bridge.h"
#include "J6_Midi.h"
#include "J6_Controller.h"
#include "picoface/ui_kit.h"
#include "j6_settings.h"
#include "j6_patchstore.h"
#include "j6_ipc.h"
#include "juno/juno.h"
#include "audio_i2s.h"      // g_i2s_underrun_count for the diagnostics footer

namespace {

// Flash lock hooks for the user patch store. Nothing to park: the erase runs
// on core0 between two audio blocks, and the store disables the interrupts
// itself.
static bool  j6_patch_lock(void)   { return true; }
static void  j6_patch_unlock(void) {}

class J6Instrument final : public picoface::Instrument {
public:
    // J6_Controller takes J6_Midi by reference
    J6Instrument() : controller_(midi_) {}

    const char* name() const override { return "PicoFaceJ6"; }

    void init() override {
        bridge_.init();
        midi_.init();
        midi_.setUiSink(&controller_);   // a value arriving over MIDI updates the display too

        // The user memories live in their own flash sector, below the two the
        // veeprom uses, and take the same core-parking hooks.
        j6_patchstore_set_lock_hooks(j6_patch_lock, j6_patch_unlock);
        j6_patchstore_init();
        controller_.useFirstFreeSlot();
    }

    uint32_t sampleRate() const override { return bridge_.currentSampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        // drain the IPC ring first, so panel and MIDI edits land on a block boundary
        uint32_t pkt;
        while (j6_ipc_pop(&pkt)) applyIpc(pkt);
        bridge_.fill_buffer_i32(out, (int) frames);
    }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(note, vel, ch); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOff(note, vel, ch); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(cc, v, ch); }
    void programChange(uint8_t ch, uint8_t p) override { midi_.onProgramChange(p, ch); }
    void pitchBend(uint8_t ch, int16_t bend) override {
        // the core hands over -8192..8191, J6_Midi expects the raw 14-bit
        // value 0..16383 with centre 8192
        midi_.onPitchBend((uint16_t)((int32_t) bend + 8192), ch);
    }

    // ---------------------------------------------------------------------
    // GUI (public)
    // ---------------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override {
        draw(d);  // first frame
    }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Encoder;
        using picoface::ui::Button;

        // Push button of the first encoder: down into the section under the
        // cursor, or back out to the section list. Moving through the menu
        // alters no parameter and therefore does not mark the settings dirty.
        if (in.pressed(Button::Sel)) {
            if (controller_.onSelectButton()) dirty_ = true;
        }

        // Push button of the third encoder: carries out whichever action the
        // PATCH WRITE page has selected - store or free - and only while that
        // page is showing. Either one erases a flash sector and stops the
        // audio for a moment, so it must not be reachable by accident; the
        // controller checks the page itself and ignores the button anywhere
        // else.
        if (in.pressed(Button::ParamB)) {
            if (controller_.onWritePage()) {
                controller_.runPatchAction();
                dirty_ = true;
            }
        }

        const int8_t d1 = in.delta(Encoder::Sel);
        const int8_t d2 = in.delta(Encoder::ParamA);
        const int8_t d3 = in.delta(Encoder::ParamB);
        if (d1) { controller_.onEncoder1(d1); dirty_ = true; }
        if (d2) { controller_.onEncoder2(d2); dirty_ = true; }
        if (d3) { controller_.onEncoder3(d3); dirty_ = true; }

        // Redraw at most every 50 ms after a change; otherwise refresh at
        // 2 Hz so the footer counters keep moving.
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) ||
            (in.nowMs - lastDrawMs_) > 500u) {
            draw(d);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // ---------------------------------------------------------------------
    // Persistence (public)
    // ---------------------------------------------------------------------

    uint16_t settingsVersion() const override { return J6_SETTINGS_VERSION; }

    size_t settingsSize() const override { return sizeof(J6SettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(J6SettingsV1)) return;
        J6SettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(J6SettingsV1)) return;
        J6SettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
    }

private:
    void applyIpc(uint32_t pkt) {
        switch (ipc_type(pkt)) {
        case IPC_CMD_J6_NOTE_ON:
            bridge_.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_J6_NOTE_OFF:
            bridge_.noteOff(ipc_d1(pkt));
            break;
        case IPC_CMD_J6_CC:
            // Only controllers that are not panel parameters get here; the
            // rest arrives as IPC_CMD_J6_PARAM.
            bridge_.controlChange(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_J6_PITCH_BEND:
            bridge_.pitchBend(ipc_d2(pkt));
            break;
        case IPC_CMD_J6_PARAM: {
            const uint8_t id = ipc_d1(pkt);
            const uint16_t v = ipc_d2(pkt);
            if (id == J6_PARAM_PROGRAM) {
                bridge_.setProgram((int32_t) v);
            } else if (id < JUNO_TOTAL_COUNT) {
                bridge_.setParameter(id, (float) v / 1000.0f);
            }
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
            snprintf(r2, sizeof r2, "IPC dropped %lu", (unsigned long) j6_ipc_dropped);
            snprintf(r3, sizeof r3, "Notes %lu", (unsigned long) bridge_.noteOnCount());
            bridge_.resetCpuPeak();
            const char* rows[4] = { r0, r1, r2, r3 };
            kit::diagnostics(d, controller_.title(), rows, 4);
            d.flush();
            return;
        }


        // '*' = the sound no longer matches the patch it came from.
        char title[20];
        snprintf(title, sizeof(title), "%s%s",
                 controller_.isEdited() ? "*" : "", controller_.title());

        d.clear();
        kit::header(d, title, controller_.pageIndex(), controller_.pageTotal());

        if (controller_.viewKind() == J6_VIEW_LIST) {
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

            // An empty name means the value describes itself (the patch does),
            // and the kit prints the value alone in that case.
            kit::panelDuo(d,
                { controller_.paramAName(), va, controller_.paramANorm() },
                { controller_.paramBName(), vb, controller_.paramBNorm() });
        }

        d.flush();  // arms the incremental push
    }

    J6_Synth_Bridge bridge_;
    J6_Midi midi_;
    // Declared after midi_: taken by reference in the constructor.
    J6_Controller controller_;
    bool dirty_ = false;
    uint32_t lastDrawMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(J6Instrument)
