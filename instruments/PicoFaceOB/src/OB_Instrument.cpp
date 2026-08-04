// OB_Instrument.cpp - adapter binding the ported OB-Xf engine to
// picoface::Instrument. Standard runtime model: core0 does audio, USB, MIDI
// and GUI, core1 is unused.
//
// Part of PicoFaceOB, GPL-3.0-or-later (see instruments/PicoFaceOB/LICENSE).

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/timer.h"

#include "picoface/instrument.h"

#include "project_config.h"    // PIN_LED
#include "audio_subsystem.h"   // SAMPLES_PER_BUFFER

#include "OB_Engine.h"
#include "OB_Ui.h"
#include "ob_ipc.h"
#include "ob_params.h"

namespace {

// 44.1 kHz. Started at 32 kHz, which measured 53% peak with six voices once
// the per-sample path was out of XIP flash; the extra rate costs roughly
// 44100/32000 of that.
//
// Spending the headroom here rather than on more voices is deliberate. The
// filter's resonance compensation is written around 44 kHz
// (`sqrt(44000 / sampleRate)` in Filter.h), so this is the engine's own
// design point. And the cutoff is clamped to `sampleRate * 0.5 - 120`: at
// 32 kHz that ceiling is 15.9 kHz, below the 19 kHz the code otherwise
// allows, so the filter could not reach its own top end.
constexpr uint32_t kSampleRate = 44100;

class OBInstrument final : public picoface::Instrument {
public:
    OBInstrument() : ui_(engine_) {
        ui_.setPeakReset([](void* ctx) { ((OBInstrument*) ctx)->loadPeak_ = 0.f; }, this);
    }

    const char* name() const override { return "PicoFaceOB"; }

    void init() override { engine_.init((float) kSampleRate); }

    uint32_t sampleRate() const override { return kSampleRate; }

    void render(int32_t* out, uint32_t frames) override {
        const uint32_t t0 = time_us_32();

        uint32_t pkt;
        while (ob_ipc_pop(&pkt)) applyIpc(pkt);

        static float buf[SAMPLES_PER_BUFFER];
        uint32_t n = frames;
        if (n > SAMPLES_PER_BUFFER) n = SAMPLES_PER_BUFFER;

        engine_.renderBlock(buf, (int) n);

        // The OB-X is mono; both channels get the same sample.
        for (uint32_t i = 0; i < n; ++i) {
            float s = buf[i];
            if (s < -1.f) s = -1.f; else if (s > 1.f) s = 1.f;
            const int32_t v = (int32_t)(s * 32767.f);
            out[i * 2 + 0] = v << 16;
            out[i * 2 + 1] = v << 16;
        }

        // CPU load of this block against its real-time budget, the same
        // measure PicoFaceYC shows.
        const uint32_t us = time_us_32() - t0;
        const float budget = (float) n * 1000000.f / (float) kSampleRate;
        const float load = (float) us * 100.f / budget;
        load_ = load;
        if (load > loadPeak_) loadPeak_ = load;
    }

    // ----------------------------------------------------------------
    // MIDI
    // ----------------------------------------------------------------

    void noteOn(uint8_t, uint8_t note, uint8_t vel) override {
        if (vel == 0) { ipc_send_ob_note_off(note); gpio_put(PIN_LED, 0); return; }
        gpio_put(PIN_LED, 1);
        ipc_send_ob_note_on(note, vel);
    }

    void noteOff(uint8_t, uint8_t note, uint8_t) override {
        gpio_put(PIN_LED, 0);
        ipc_send_ob_note_off(note);
    }

    void controlChange(uint8_t, uint8_t cc, uint8_t value) override {
        switch (cc) {
        case 1:   ipc_send_ob_modwheel(value); return;
        case 64:  ipc_send_ob_sustain(value >= 64 ? 1 : 0); return;
        case 120:
        case 123: ipc_send_ob_all_notes_off(); return;
        default: break;
        }
        // Panel parameters share one table for both directions, so send and
        // receive cannot drift apart (ARCHITECTURE.md section 6a).
        for (uint8_t i = 0; i < OB_PARAM_COUNT; ++i) {
            if (obParams[i].cc == cc) {
                ipc_send_ob_param(i, (float) value * (1.f / 127.f));
                return;
            }
        }
    }

    void pitchBend(uint8_t, int16_t bend) override {
        ipc_send_ob_pitchbend((uint16_t)((int32_t) bend + 8192));
    }

    // ----------------------------------------------------------------
    // GUI
    // ----------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override { ui_.drawNow(d); }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        ui_.setLoad(load_, loadPeak_);
        ui_.tick(d, in);
    }

    // ----------------------------------------------------------------
    // Persistence: the whole parameter set, one float per parameter
    // ----------------------------------------------------------------

    // Version 2: OB_OSC1_PITCH was added to the parameter set, so a record
    // written before that has the wrong length and must be discarded.
    uint16_t settingsVersion() const override { return 2; }
    size_t settingsSize() const override { return sizeof(float) * OB_PARAM_COUNT; }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(float) * OB_PARAM_COUNT) return;
        float tmp[OB_PARAM_COUNT];
        for (uint8_t i = 0; i < OB_PARAM_COUNT; ++i) tmp[i] = engine_.getParam(i);
        memcpy(buffer, tmp, sizeof(tmp));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(float) * OB_PARAM_COUNT) return;
        float tmp[OB_PARAM_COUNT];
        memcpy(tmp, buffer, sizeof(tmp));
        for (uint8_t i = 0; i < OB_PARAM_COUNT; ++i) engine_.setParam(i, tmp[i]);
    }

private:
    void applyIpc(uint32_t pkt) {
        switch (ipc_type(pkt)) {
        case IPC_CMD_OB_NOTE_ON:
            engine_.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_OB_NOTE_OFF:
            engine_.noteOff(ipc_d1(pkt));
            break;
        case IPC_CMD_OB_PARAM:
            engine_.setParam(ipc_d1(pkt), ipc_u16_to_f(ipc_d2(pkt)));
            break;
        case IPC_CMD_OB_SUSTAIN:
            engine_.setSustain(ipc_d1(pkt) != 0);
            break;
        case IPC_CMD_OB_PITCHBEND:
            engine_.setPitchBend(((float) ipc_d2(pkt) - 8192.f) * (1.f / 8192.f));
            break;
        case IPC_CMD_OB_ALL_NOTES_OFF:
            engine_.allNotesOff();
            break;
        case IPC_CMD_OB_MODWHEEL:
            engine_.setModWheel((float) ipc_d1(pkt) * (1.f / 127.f));
            break;
        default:
            break;
        }
    }

    OB_Engine engine_;
    OB_Ui     ui_;
    float     load_     = 0.f;
    float     loadPeak_ = 0.f;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(OBInstrument)
