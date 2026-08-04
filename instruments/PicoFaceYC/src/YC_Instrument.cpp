// YC_Instrument.cpp - adapter binding the Yamaha reface YC organ to picoface::Instrument.
//
// PicoFaceYC uses the standard runtime model: the core owns board, USB, MIDI,
// display and encoders on core0 and calls uiTick(); core1 is unused. Until the
// migration this instrument owned the whole user interface on core1 and ran a
// blocking front panel loop there - see YC_Ui.h and include/ipc.h for what that
// cost and how it was undone.

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "picoface/instrument.h"

#include "audio_subsystem.h"   // SAMPLES_PER_BUFFER
#include "project_config.h"    // PIN_LED
#include "ipc.h"
#include "midi_reface.h"
#include "settings.h"
#include "YC_Controller.h"
#include "YC_Synth_Bridge.h"
#include "YC_Ui.h"

namespace {

class YCInstrument final : public picoface::Instrument {
public:
    // YC_Controller takes bridge and MIDI layer by reference, YC_Ui takes
    // controller and bridge - hence the member order below.
    YCInstrument() : controller_(bridge_, refaceMidi_), ui_(controller_, bridge_) {}

    const char* name() const override { return "PicoFaceYC"; }

    void init() override {
        // Count watchdog-caused reboots in a scratch register that survives them.
        if (watchdog_caused_reboot()) { watchdog_hw->scratch[0] += 1; } else { watchdog_hw->scratch[0] = 0; }
        bridge_.init();
        refaceMidi_.init(&bridge_);
        // Defaults in the engine act as the fallback when no valid record exists.
        settings_boot_restore(&bridge_, &refaceMidi_);
    }

    uint32_t sampleRate() const override { return (uint32_t) YC_SAMPLE_RATE; }

    void render(int32_t* out, uint32_t frames) override {
        // The watchdog can only be armed once audio is running; the first
        // rendered block is exactly that moment.
        if (!watchdogArmed_) { watchdog_enable(4000, false); watchdogArmed_ = true; }
        watchdog_update();

        // Drain the ring first: MIDI dispatch and panel edits run on this core
        // but outside the producer, so they are applied at block boundaries.
        uint32_t pkt;
        while (yc_ipc_pop(&pkt)) applyIpc(pkt);

        // The YC engine renders float; convert to the packed int32 the I2S pool wants.
        static float buf[SAMPLES_PER_BUFFER * 2];
        uint32_t n = frames;
        // The pool never hands out more than SAMPLES_PER_BUFFER; this only guards the fixed buffer.
        if (n > SAMPLES_PER_BUFFER) n = SAMPLES_PER_BUFFER;
        bridge_.fill_buffer(buf, (int) n);
        for (uint32_t i = 0; i < n; ++i) {
            int32_t dl = (int32_t)(buf[i * 2 + 0] * 32767.0f);
            int32_t dr = (int32_t)(buf[i * 2 + 1] * 32767.0f);
            if (dl < -32768) dl = -32768; else if (dl > 32767) dl = 32767;
            if (dr < -32768) dr = -32768; else if (dr > 32767) dr = 32767;
            out[i * 2 + 0] = dl << 16;
            out[i * 2 + 1] = dr << 16;
        }
    }

    // ----------------------------------------------------------------
    // MIDI - the core parses both wires and dispatches here; the reface
    // layer does channel filtering, transpose and panel mapping and pushes
    // onto the ring. The picoface interface passes the channel first,
    // RefaceMidi expects it last.
    // ----------------------------------------------------------------

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override {
        gpio_put(PIN_LED, vel > 0 ? 1 : 0);
        refaceMidi_.onNoteOn(note, vel, ch);
    }

    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override {
        gpio_put(PIN_LED, 0);
        refaceMidi_.onNoteOff(note, vel, ch);
    }

    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { refaceMidi_.onControlChange(cc, v, ch); }

    // The core hands over -8192..8191, RefaceMidi expects the raw 14-bit value
    // 0..16383 with centre 8192.
    void pitchBend(uint8_t ch, int16_t bend) override { refaceMidi_.onPitchBend((uint16_t)((int32_t) bend + 8192), ch); }

    void sysEx(const uint8_t* data, size_t length) override { refaceMidi_.onSysEx(data, (uint16_t) length); }

    void realtime(uint8_t status) override { refaceMidi_.onRealtime(status); }
    void midiActivity() override { refaceMidi_.notifyActivity(); }

    // programChange stays unimplemented: the YC has none per its MIDI
    // implementation chart.

    // ----------------------------------------------------------------
    // GUI
    // ----------------------------------------------------------------

    void uiInit(picoface::ui::Display& d) override {
        // First frame, so the panel is on screen right after the splash
        // instead of one tick later.
        ui_.drawNow(d);
    }

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        ui_.tick(d, in);
        // Active sensing TX/RX supervision and the debounced settings write-back.
        // Both are cheap polls and only need to run at UI rate; this is the one
        // periodic core0 callback the instrument gets.
        refaceMidi_.tick();
        settings_task(&bridge_, &refaceMidi_);
    }

    // settingsSize() stays 0: this instrument writes its own veeprom record
    // from settings_task() rather than handing a buffer to the core.

private:
    void applyIpc(uint32_t pkt) {
        switch (ipc_type(pkt))
        {
        case IPC_CMD_YC_NOTE_ON:
            bridge_.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_YC_NOTE_OFF:
            bridge_.noteOff(ipc_d1(pkt));
            break;
        case IPC_CMD_YC_PANEL_UPDATE:
            bridge_.setParam(ipc_d1(pkt), ipc_d2(pkt));
            break;
        case IPC_CMD_YC_SUSTAIN:
            bridge_.setSustain(ipc_d1(pkt) != 0);
            break;
        case IPC_CMD_YC_ALL_NOTES_OFF:
            bridge_.allNotesOff();
            break;
        case IPC_CMD_YC_ROTARY_TARGET:
            bridge_.setRotaryTarget(ipc_d1(pkt));
            break;
        case IPC_CMD_YC_MIDI_CTRL_MODE:
            break;  // not implemented yet
        case IPC_CMD_YC_PITCH_BEND:
            break;  // pitch bend not implemented in the tonegen yet
        default:
            break;
        }
    }

    YC_Synth_Bridge bridge_;
    RefaceMidi      refaceMidi_;   // channel/CC/SysEx/active sensing
    YC_Controller   controller_;
    YC_Ui           ui_;
    bool            watchdogArmed_ = false;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(YCInstrument)
