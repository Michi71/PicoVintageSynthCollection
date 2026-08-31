// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// RD_Instrument.cpp - adapter binding the Roland RD / MKS-20 classes to
// picoface::Instrument. Unlike the other instruments the RD engine uses core1
// as a dedicated RAM-resident voice worker, and it switches its sample rate at
// runtime depending on the selected instrument. Both are handled through the
// optional hooks of the interface.

#include <cstdio>
#include <cstring>
#include <cmath>

#include "pico/multicore.h"

#include "picoface/instrument.h"
#include "pico_hw.h"
#include "RD_Synth_Bridge_v2.h"
#include "RD_Midi.h"
#include "RD_Controller.h"
#include "RD_Display.h"
#include "rd_settings.h"
#include "rd_params.h"
#include "rd_ipc_local.h"
#include "rd_engine/rd_new_engine.h"
#include "audio_i2s.h" // g_i2s_underrun_count for the diagnostics footer

namespace {

// Entry point of the core1 voice worker. Kept at namespace scope because
// multicore_launch_core1 takes a plain function pointer.
void rd_core1_main(void) {
    // FPSCR is per-core: without this, any future float use on this core (or
    // compiler-generated FP) reintroduces the denormal slow path that
    // pico_init() guards core0 against.
    pico_fpu_ftz_enable();
    // worker_loop() is RAM-resident: a flash spin loop (plus the long-branch
    // veneer) adds doorbell jitter and steals QSPI bandwidth from the sample
    // ROM stream.
    RdNewEngine::worker_loop();
}

class RDInstrument final : public picoface::Instrument {
public:
    // RD_Controller takes RD_Midi by reference
    RDInstrument() : controller_(midi_) {}

    const char* name() const override { return "PicoFaceRD"; }

    void init() override {
        bridge_.init();

        // Core1 renders the odd voice indices of the engine.
        RdNewEngine::worker_enable(bridge_.engineForWorker(), true);
        // Reset core1 into the bootrom holding pen first: after a debugger
        // restart (core0 only) the launch handshake would otherwise hang.
        multicore_reset_core1();
        multicore_launch_core1(rd_core1_main);

        midi_.init();
    }

    uint32_t sampleRate() const override { return bridge_.currentSampleRate(); }

    void render(int32_t* out, uint32_t frames) override {
        // drain the IPC ring first, so panel and MIDI edits land on a block
        // boundary
        uint32_t pkt;
        while (rd_ipc_pop(&pkt)) applyIpc(pkt);
        bridge_.fill_buffer_i32(out, (int) frames);
    }

    // the engine switches between 20 kHz and 32 kHz depending on the selected
    // instrument; the core defers the hardware switch until the DMA pipeline
    // has drained
    bool consumeSampleRateChange() override { return bridge_.consumeSampleRateChanged(); }

    // an underrun means the voice count outran the CPU - drop voices
    // immediately
    void onAudioUnderrun() override { bridge_.voiceGovernorEmergency(); }

    // a flash write stalls the CPU for milliseconds; only write while nothing
    // is sounding
    bool settingsSaveAllowed() const override { return bridge_.activeVoices() == 0; }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOn(note, vel, ch); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override { midi_.onNoteOff(note, vel, ch); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { midi_.onControlChange(cc, v, ch); }
    void programChange(uint8_t ch, uint8_t p) override { midi_.onProgramChange(p, ch); }
    void pitchBend(uint8_t ch, int16_t bend) override { midi_.onPitchBend((uint16_t)((int32_t) bend + 8192), ch); }

    // ---------------------------------------------------------------------
    // GUI (public)
    // ---------------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override { draw(d); }  // first frame

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Encoder;
        // The RD panel has no push-button action: the first encoder cycles the pages.
        const int8_t d1 = in.delta(Encoder::Sel), d2 = in.delta(Encoder::ParamA), d3 = in.delta(Encoder::ParamB);
        if (d1) { controller_.onEncoder1(d1); dirty_ = true; }
        if (d2) { controller_.onEncoder2(d2); dirty_ = true; }
        if (d3) { controller_.onEncoder3(d3); dirty_ = true; }
        // Refresh quickly after user input, but at least every 500 ms as a keep-alive.
        if ((dirty_ && (in.nowMs - lastDrawMs_) > 50u) || (in.nowMs - lastDrawMs_) > 500u) {
            draw(d);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        }
    }

    // ---------------------------------------------------------------------
    // Persistence (public)
    // ---------------------------------------------------------------------

    uint16_t settingsVersion() const override { return RD_SETTINGS_VERSION; }
    size_t settingsSize() const override { return sizeof(RdSettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(RdSettingsV1)) return;
        RdSettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(RdSettingsV1)) return;
        RdSettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        // The import re-sends everything through the normal IPC path; render()
        // drains the ring before the first real block, and the sample-rate hook
        // handles a restored 32k instrument.
        controller_.importSettings(s);
    }

private:
    // Decode one packed 32-bit command word and forward it to the bridge
    // (called from render(), on a block boundary).
    void applyIpc(uint32_t pkt) {
        switch (ipc_type(pkt)) {
            case IPC_CMD_DX_NOTE_ON:
                bridge_.noteOn(ipc_d1(pkt), ipc_d2(pkt));
                break;
            case IPC_CMD_DX_NOTE_OFF:
                bridge_.noteOff(ipc_d1(pkt));
                break;
            case IPC_CMD_DX_CC:
                // Only sustain (CC 64) and all-notes-off (CC 123) are handled here.
                if (ipc_d1(pkt) == 64) bridge_.sustain((uint8_t) ipc_d2(pkt));
                else if (ipc_d1(pkt) == 123) bridge_.allNotesOff();
                break;
            case IPC_CMD_DX_PITCH_BEND:
                bridge_.pitchBend(ipc_d2(pkt));
                break;
            case IPC_CMD_DX_PARAM: {
                const uint8_t id = ipc_d1(pkt);
                const uint16_t v = ipc_d2(pkt);
                if (id == RD_PARAM_INSTRUMENT) bridge_.setInstrument((uint8_t) v);
                else if (id == RD_PARAM_VOICE_MODE) bridge_.setVoiceMode((uint8_t) v);
                else if (id == RD_PARAM_MASTER_TUNE) bridge_.setMasterTune((int) v - 50);
                else bridge_.setFxParam(id, (uint8_t) v);  // everything else is an FX parameter
                break;
            }
            default:
                break;  // unknown command: ignore
        }
    }

    void draw(picoface::ui::Display& d)
    {
        static char nm[24];
        static RdUiModel m;

        // Choose header title: BANK on the PATCH page, page name elsewhere.
        // Patch names arrive as "MKS-20: Piano 1" -- the bank prefix replaces the
        // page name in the header, the body shows number + bare name (fits 128px).
        const char* titleName = controller_.pageName();
        const char* bareName  = nm;   // meaningful only on the PATCH page
        if (controller_.currentPage() == RdPage::PATCH) {
            bridge_.instrumentName(nm, sizeof(nm));
            bareName = nm;
            char* colon = strchr(nm, ':');
            if (colon != nullptr) {
                *colon = '\0';            // bank = prefix
                bareName = colon + 1;      // bare name past the colon
                if (bareName[0] == ' ') ++bareName;   // skip one leading space
                titleName = nm;
            } else {
                titleName = controller_.pageName();
                bareName  = nm;
            }
        }

        // Header: "<PAGENAME|BANK> <sr>k" plus "<n>/<COUNT>" page indicator.
        snprintf(m.title, sizeof(m.title), "%s %luk", titleName, (unsigned long)(bridge_.currentSampleRate() / 1000));
        snprintf(m.page, sizeof(m.page), "%d/%d", (int) controller_.currentPage() + 1, (int) RdPage::COUNT);

        // Body lines depend on the active page.
        if (controller_.currentPage() == RdPage::PATCH) {
            snprintf(m.lineA, sizeof(m.lineA), "%02d %s", (int) bridge_.instrument() + 1, bareName);
            snprintf(m.lineB, sizeof(m.lineB), "Volume %d%%", (int) controller_.param3Value());
        } else if (controller_.currentPage() == RdPage::VOICES) {
            static const char* const kVoiceModeNames[5] = {"8", "16", "24", "32", "Auto"};
            uint8_t vm = controller_.param2Value();
            if (vm > 4) vm = 4;   // clamp
            snprintf(m.lineA, sizeof(m.lineA), "Voices %s", kVoiceModeNames[vm]);
            snprintf(m.lineB, sizeof(m.lineB), "Act %d/%d", bridge_.activeVoices(), (int) bridge_.voiceLimit());
        } else if (controller_.currentPage() == RdPage::TUNE) {
            int cents = (int) controller_.param2Value() - 50;
            snprintf(m.lineA, sizeof(m.lineA), "Tune %+dc", cents);
            snprintf(m.lineB, sizeof(m.lineB), "A4 %.1fHz", 440.0f * exp2f(cents / 1200.0f));
        } else if (controller_.currentPage() == RdPage::SYS) {
            snprintf(m.lineA, sizeof(m.lineA), "DAC Flt %s", (controller_.param2Value() != 0 ? "ON" : "OFF"));
            if (controller_.param3Value() == 16) {
                snprintf(m.lineB, sizeof(m.lineB), "MIDI Ch Omni");
            } else {
                snprintf(m.lineB, sizeof(m.lineB), "MIDI Ch %d", (int) controller_.param3Value() + 1);
            }
        } else if (controller_.currentPage() == RdPage::CHORUS ||
                   controller_.currentPage() == RdPage::TREMOLO ||
                   controller_.currentPage() == RdPage::PHASER) {
            // Rates in Hz rather than percent. Two of the three are measured
            // figures out of the service notes rather than a scale of our own
            // (see rd_params.h), so the number means something -- and the TUNE
            // page already sets the precedent of showing the physical value.
            const float x = (float) controller_.param3Value() * 0.01f;
            float hz;
            switch (controller_.currentPage()) {
                case RdPage::CHORUS:  hz = rd_chorus_rate_hz(x); break;
                case RdPage::TREMOLO: hz = rd_trem_rate_hz(x);   break;
                default:              hz = rd_phaser_rate_hz(x); break;
            }
            snprintf(m.lineA, sizeof(m.lineA), "%s %d%%", controller_.param2Name(), (int) controller_.param2Value());
            snprintf(m.lineB, sizeof(m.lineB), "%s %.2fHz", controller_.param3Name(), (double) hz);
        } else {
            // Continuous params are stored as percent (0..100).
            snprintf(m.lineA, sizeof(m.lineA), "%s %d%%", controller_.param2Name(), (int) controller_.param2Value());
            snprintf(m.lineB, sizeof(m.lineB), "%s %d%%", controller_.param3Name(), (int) controller_.param3Value());
        }

        // Footer: instrument, CPU peak, underruns, dropped IPC packets, active voices, note-ons.
        snprintf(m.footer, sizeof(m.footer), "%d P%d U%lu D%lu A%d N%lu",
                 (int) bridge_.instrument(),
                 (int) bridge_.cpuLoadPeakPercent(),
                 (unsigned long) g_i2s_underrun_count,
                 (unsigned long) rd_ipc_dropped,
                 (int) bridge_.activeVoices(),
                 (unsigned long) bridge_.noteOnCount());

        rd_display_page(d.raw(), m);
        d.flush();   // arms the incremental push
    }

    RD_Synth_Bridge bridge_;
    RD_Midi         midi_;
    RD_Controller   controller_;   // declared after midi_: taken by reference in the constructor
    bool            dirty_ = false;
    uint32_t        lastDrawMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(RDInstrument)
