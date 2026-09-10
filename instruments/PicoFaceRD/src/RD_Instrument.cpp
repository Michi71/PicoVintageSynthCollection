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
#include "picoface/ui_kit.h"
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
    void programChange(uint8_t ch, uint8_t p) override {
        // The panel draws from its own selection, so it has to follow a program
        // change too -- otherwise the screen would keep showing the last thing
        // the encoder chose while the engine played something else.
        const int idx = midi_.onProgramChange(p, ch);
        if (idx >= 0) { controller_.adoptInstrument((uint8_t) idx); dirty_ = true; }
    }
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
        namespace kit = picoface::ui::kit;

        // The developer's numbers went from a strip at the bottom of every page
        // to a page of their own (#161: that strip cost the body nine rows on
        // a screen with none to spare). Formatted here because only this
        // instrument knows what its numbers are; 6x12 holds 21 characters.
        if (controller_.isDiagPage()) {
            char r0[22], r1[22], r2[22], r3[22];
            snprintf(r0, sizeof r0, "Instr %d  CPU %d%%",
                     (int) bridge_.instrument(), (int) bridge_.cpuLoadPeakPercent());
            snprintf(r1, sizeof r1, "Underruns %lu", (unsigned long) g_i2s_underrun_count);
            snprintf(r2, sizeof r2, "IPC dropped %lu", (unsigned long) rd_ipc_dropped);
            snprintf(r3, sizeof r3, "Voices %d  notes %lu",
                     (int) bridge_.activeVoices(), (unsigned long) bridge_.noteOnCount());
            const char* rows[4] = { r0, r1, r2, r3 };
            kit::diagnostics(d, "DIAG", rows, 4);
            d.flush();
            return;
        }
        using Param = kit::Param;

        static char nm[24];

        // Patch names arrive as "MKS-20: Piano 1" -- the bank prefix goes in
        // the header where the page name would be, the bare name into the body.
        //
        // Everything the header and the patch line show comes from the panel's
        // own selection, not from the engine. The engine picks a change up on
        // the next audio block, which is one pass of the main loop LATER than
        // the tick that drew -- so drawing from it showed the previous
        // instrument and cleared the dirty flag, and the screen then sat there
        // until the 500 ms keep-alive. That was the "sehr lange" on hardware.
        const uint8_t shown = controller_.instrument();
        const char* titleName = controller_.pageName();
        const char* bareName  = nm;
        const char* bankName  = "";
        if (controller_.currentPage() == RdPage::PATCH) {
            RD_Synth_Bridge::patchNameOf(shown, nm, sizeof(nm));
            bareName = nm;
            char* colon = strchr(nm, ':');
            if (colon != nullptr) {
                *colon = '\0';            // bank = prefix
                bareName = colon + 1;      // bare name past the colon
                if (bareName[0] == ' ') ++bareName;   // skip one leading space
                bankName = nm;
                titleName = nm;
            }
        }

        char title[24];
        snprintf(title, sizeof(title), "%s %luk", titleName,
                 (unsigned long)(RD_Synth_Bridge::sampleRateOf(shown) / 1000));


        d.clear();
        kit::header(d, title, (int) controller_.currentPage(), (int) RdPage::COUNT);

        char va[24], vb[24];
        Param a{}, b{};

        switch (controller_.currentPage()) {
        case RdPage::PATCH:
            // The instrument name gets the full width, with its bank under it.
            snprintf(va, sizeof(va), "%02d %s", (int) shown + 1, bareName);
            snprintf(vb, sizeof(vb), "%d%%", (int) controller_.param3Value());
            kit::panelName(d, va, bankName,
                           { "Volume", vb, controller_.param3Value() / 100.0f });
            break;

        case RdPage::VOICES: {
            static const char* const kVoiceModeNames[5] = {"8", "16", "24", "32", "Auto"};
            uint8_t vm = controller_.param2Value();
            if (vm > 4) vm = 4;   // clamp
            snprintf(va, sizeof(va), "%s", kVoiceModeNames[vm]);
            snprintf(vb, sizeof(vb), "%d/%d", bridge_.activeVoices(),
                     (int) bridge_.voiceLimit());
            // Neither has a dial position: one steps through five settings,
            // the other is a reading rather than a control.
            kit::panelDuo(d, { "Voices", va }, { "Active", vb });
            break;
        }

        case RdPage::TUNE: {
            const int cents = (int) controller_.param2Value() - 50;
            snprintf(va, sizeof(va), "%+dc", cents);
            snprintf(vb, sizeof(vb), "%.1fHz", (double)(440.0f * exp2f(cents / 1200.0f)));
            kit::panelDuo(d, { "Tune", va, controller_.param2Value() / 100.0f },
                             { "A4", vb });
            break;
        }

        case RdPage::SYS:
            snprintf(va, sizeof(va), "%s", controller_.param2Value() != 0 ? "ON" : "OFF");
            if (controller_.param3Value() == 16) snprintf(vb, sizeof(vb), "Omni");
            else snprintf(vb, sizeof(vb), "%d", (int) controller_.param3Value() + 1);
            kit::panelDuo(d, { "DAC Flt", va }, { "MIDI Ch", vb });
            break;

        case RdPage::CHORUS:
        case RdPage::TREMOLO:
        case RdPage::PHASER: {
            // Rates in Hz rather than percent. Two of the three are measured
            // figures out of the service notes rather than a scale of our own
            // (see rd_params.h), so the number means something -- and the TUNE
            // page already sets the precedent of showing the physical value.
            // The stored value IS the 0.05 Hz grid index, so this is exact --
            // no scale conversion to disagree with the engine about.
            const uint8_t rateId = rateParamId(controller_.currentPage());
            const uint8_t lo = rd_rate_idx_min(rateId), hi = rd_rate_idx_max(rateId);
            const float hz = rd_rate_hz_from_idx(controller_.param3Value());
            snprintf(va, sizeof(va), "%d%%", (int) controller_.param2Value());
            snprintf(vb, sizeof(vb), "%.2fHz", (double) hz);
            kit::panelDuo(d,
                { controller_.param2Name(), va, controller_.param2Value() / 100.0f },
                { controller_.param3Name(), vb,
                  (hi > lo) ? (float)(controller_.param3Value() - lo) / (float)(hi - lo) : -1.0f });
            break;
        }

        default:
            // Continuous params are stored as percent (0..100).
            snprintf(va, sizeof(va), "%d%%", (int) controller_.param2Value());
            snprintf(vb, sizeof(vb), "%d%%", (int) controller_.param3Value());
            kit::panelDuo(d,
                { controller_.param2Name(), va, controller_.param2Value() / 100.0f },
                { controller_.param3Name(), vb, controller_.param3Value() / 100.0f });
            break;
        }

        d.flush();   // arms the incremental push
    }

    // Which stored parameter carries the rate of the effect a page edits. Only
    // used to scale the knob: the three effects have different rate ranges.
    static uint8_t rateParamId(RdPage p)
    {
        switch (p) {
        case RdPage::CHORUS:  return RD_PARAM_CHORUS_RATE;
        case RdPage::TREMOLO: return RD_PARAM_TREM_RATE;
        case RdPage::PHASER:  return RD_PARAM_PHASER_RATE;
        default:              return RD_PARAM_CHORUS_RATE;
        }
    }

    RD_Synth_Bridge bridge_;
    RD_Midi         midi_;
    RD_Controller   controller_;   // declared after midi_: taken by reference in the constructor
    bool            dirty_ = false;
    uint32_t        lastDrawMs_ = 0;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(RDInstrument)
