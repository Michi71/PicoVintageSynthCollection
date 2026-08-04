// DX_Instrument.cpp - adapter binding the Yamaha reface DX to picoface::Instrument.
//
// PicoFaceDX uses the standard runtime model: the core owns board, USB, MIDI,
// display and encoders on core0 and calls uiTick(); core1 is unused. Until the
// migration this instrument owned the whole user interface on core1 and ran a
// blocking front panel loop there - see DX_Ui.h and include/ipc.h for what that
// cost and how it was undone.

#include <cstdint>

#include "pico/stdlib.h"

#include "picoface/instrument.h"

#include "project_config.h"    // PIN_LED
#include "dx_engine/dx_engine_config.h"   // SAMPLE_RATE
#include "DX_Controller.h"
#include "DX_Synth_Bridge.h"
#include "DX_Ui.h"
#include "ipc.h"
#include "midi_reface.h"
#include "presets.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Two values that are neither patch parameters nor engine state, and were
// core1 globals in main.cpp before the migration. They stay free functions
// with C linkage because midi_reface.cpp, settings.cpp and DX_Ui.cpp all reach
// for them and none of them knows the adapter type.
// ---------------------------------------------------------------------------
namespace {
int g_octave = 0;          // UI octave transpose, -2..+2, applied in RefaceMidi
int g_masterVolume = 100;  // mirror of the engine-side value, for UI and autosave
} // namespace

extern "C" void ui_set_octave(int oct)
{
    if (oct < -2) oct = -2;
    if (oct > 2) oct = 2;
    g_octave = oct;
}

extern "C" int ui_get_octave(void) { return g_octave; }

// Master volume is a device setting, not a patch parameter: it reaches the
// engine through the ring and is persisted with the settings record, but is
// never written into RDX_Patch, so presets and SysEx voice dumps leave it alone.
extern "C" void ui_set_master_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_masterVolume = vol;
    ipc_send_dx_master_volume((uint8_t) vol);
}

extern "C" int ui_get_master_volume(void) { return g_masterVolume; }

namespace {

class DXInstrument final : public picoface::Instrument {
public:
    // DX_Controller takes the bridge, DX_Ui takes controller, bridge and the
    // MIDI layer (for the Program Change it sends on a preset pick) - hence the
    // member order below.
    DXInstrument() : controller_(bridge_), ui_(controller_, bridge_, refaceMidi_) {}

    const char* name() const override { return "PicoFaceDX"; }

    void init() override {
        bridge_.init();
        refaceMidi_.init(&bridge_);
        // The engine defaults act as the fallback when no valid record exists.
        settings_boot_restore(&bridge_, &refaceMidi_);
    }

    uint32_t sampleRate() const override { return (uint32_t) SAMPLE_RATE; }

    void render(int32_t* out, uint32_t frames) override {
        // Drain the ring first: MIDI dispatch, SysEx and panel edits run on this
        // core but outside the producer, so they are applied at block boundaries.
        uint32_t pkt;
        while (dx_ipc_pop(&pkt)) applyIpc(pkt);

        // The DX bridge writes the packed int32 stereo the I2S pool wants, so
        // there is no conversion pass here.
        bridge_.fill_buffer_i32(out, (int) frames);
    }

    // ----------------------------------------------------------------
    // MIDI - the core parses both wires and dispatches here; the reface
    // layer does channel filtering, transpose and SysEx and pushes onto
    // the ring. The picoface interface passes the channel first,
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

    void programChange(uint8_t ch, uint8_t program) override { refaceMidi_.onProgramChange(program, ch); }

    // The core hands over -8192..8191, RefaceMidi expects the raw 14-bit value
    // 0..16383 with centre 8192.
    void pitchBend(uint8_t ch, int16_t bend) override { refaceMidi_.onPitchBend((uint16_t)((int32_t) bend + 8192), ch); }

    void sysEx(const uint8_t* data, size_t length) override { refaceMidi_.onSysEx(data, (uint16_t) length); }

    void realtime(uint8_t status) override { refaceMidi_.onRealtime(status); }
    void midiActivity() override { refaceMidi_.notifyActivity(); }

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
        switch (ipc_type(pkt)) {
        case IPC_CMD_DX_NOTE_ON:
            bridge_.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_DX_NOTE_OFF:
            bridge_.noteOff(ipc_d1(pkt));
            break;
        case IPC_CMD_DX_PARAM: {
            const uint8_t val = (uint8_t) ipc_d2(pkt);
            RDX_Patch& p = bridge_.patch();
            switch (ipc_d1(pkt)) {
            case DX_PARAM_OP1_FREQ:     p.ops[0].freqCoarse = val; break;
            case DX_PARAM_OP1_LEVEL:    p.ops[0].outLevel   = val; break;
            case DX_PARAM_OP2_FREQ:     p.ops[1].freqCoarse = val; break;
            case DX_PARAM_OP2_LEVEL:    p.ops[1].outLevel   = val; break;
            case DX_PARAM_OP3_FREQ:     p.ops[2].freqCoarse = val; break;
            case DX_PARAM_OP3_LEVEL:    p.ops[2].outLevel   = val; break;
            case DX_PARAM_OP4_FREQ:     p.ops[3].freqCoarse = val; break;
            case DX_PARAM_OP4_LEVEL:    p.ops[3].outLevel   = val; break;
            case DX_PARAM_LFO_SPEED:    p.common.lfoSpeed   = val; break;
            case DX_PARAM_LFO_PMD:      p.common.lfoPMD     = val; break;
            case DX_PARAM_ALGO:         p.common.algorithm  = val; break;
            case DX_PARAM_OP1_FEEDBACK: p.ops[0].feedback   = val; break;
            default: break;
            }
            break;
        }
        case IPC_CMD_DX_PITCH_BEND:
            bridge_.updatePB((int) ipc_d2(pkt) - 8192);
            break;
        case IPC_CMD_DX_CC:
            bridge_.processCC(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_DX_RAW_WRITE: {
            const uint8_t byteOffset = ipc_d1(pkt);
            const uint8_t blockSel   = ipc_raw_write_block_sel(pkt);
            const uint8_t value      = ipc_raw_write_value(pkt);
            RDX_Patch& p = bridge_.patch();
            if (blockSel == 1 && byteOffset < sizeof(RDX_Common)) {
                reinterpret_cast<uint8_t*>(&p.common)[byteOffset] = value;
            } else if (blockSel >= 2 && blockSel <= 5 && byteOffset < sizeof(RDX_OpParams)) {
                reinterpret_cast<uint8_t*>(&p.ops[blockSel - 2])[byteOffset] = value;
            }
            break;
        }
        case IPC_CMD_DX_PATCH_APPLY:
            preset_apply(&bridge_);
            break;
        case IPC_CMD_DX_MASTER_VOLUME:
            bridge_.setMasterVolume((uint8_t) ipc_d2(pkt));
            break;
        case IPC_CMD_DX_MASTER_TUNE: {
            const uint16_t raw = ipc_d2(pkt);
            float cents = ((int) raw - 1024) * 0.1f;
            if (cents < -102.4f) cents = -102.4f;
            if (cents > 102.3f) cents = 102.3f;
            bridge_.setMasterTune(cents / 100.0f);
            break;
        }
        default:
            break;
        }
    }

    DX_Synth_Bridge bridge_;
    RefaceMidi      refaceMidi_;   // channel/CC/SysEx/active sensing
    DX_Controller   controller_;
    DX_Ui           ui_;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(DXInstrument)
