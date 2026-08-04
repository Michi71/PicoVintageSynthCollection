// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// CP_Instrument.cpp - adapter binding the Yamaha reface CP electric piano
// (mdaEPiano engine plus the reface CP effect chain) to picoface::Instrument.
//
// PicoFaceCP uses the standard runtime model: the core owns board, USB, MIDI,
// display and encoders on core0 and calls uiTick(); core1 is unused. Until the
// migration this instrument owned the whole user interface on core1 and ran a
// blocking front panel loop there - see CP_Ui.h and include/ipc.h for what that
// cost and how it was undone.

#include <cstdio>
#include <cmath>

#include "pico/stdlib.h"

#include "picoface/instrument.h"

#include "project_config.h"    // PIN_LED
#include "audio_subsystem.h"   // SAMPLES_PER_BUFFER, SAMPLING_RATE
#include "ipc.h"
#include "midi_reface.h"
#include "mdaEPiano.h"
#include "presets.h"
#include "reface_cp_chain.h"
#include "cp_audio.h"          // cp_process_block_i16()
#include "settings.h"
#include "CP_Ui.h"

// ---------------------------------------------------------------------------
// File-level objects
//
// Deliberately at file scope rather than members of the instrument class:
// cp_render_block() below is RAM-resident and must stay that way, and reaching
// the engine through the singleton would put a flash-resident indirection back
// into the audio hot path.
// ---------------------------------------------------------------------------
static mdaEPiano ep(96);
static RefaceCpChain cp_fx;    // reface CP effect chain, post-processes the engine output
static RefaceMidi refaceMidi;  // channel filter, CC map, SysEx, active sensing
static volatile int g_octave = 0;

// Octave transpose, shared with midi_reface.cpp, settings.cpp and CP_Ui.cpp.
// Plain C linkage because midi_reface.cpp declares it that way.
extern "C" void ui_set_octave(int oct) {
    if (oct < -2) oct = -2;
    if (oct >  2) oct =  2;
    g_octave = oct;
}

extern "C" int ui_get_octave(void) {
    return g_octave;
}

// -----------------------------------------------------------------------------
// Render block
// -----------------------------------------------------------------------------
// Kept out of line and RAM resident on purpose: this is the audio producer
// hot path and must not be pulled into flash where an inline expansion could
// stall during flash operations.
static void __no_inline_not_in_flash_func(cp_render_block)(int32_t* out, uint32_t frames)
{
    // Always == SAMPLES_PER_BUFFER in this build; clamp anyway, because
    // mdaEPiano::process() takes no length and always writes exactly
    // I2S_BUFFER_WORDS frames.
    uint32_t n = frames;
    if (n > SAMPLES_PER_BUFFER) n = SAMPLES_PER_BUFFER;

    int16_t l[SAMPLES_PER_BUFFER];
    int16_t r[SAMPLES_PER_BUFFER];

    ep.process(&r[0], &l[0]);   // (outputs_r, outputs_l) -- deliberate, see the pipeline changelog
    cp_process_block_i16(cp_fx, &l[0], &r[0], (int) n);

    for (uint32_t i = 0; i < n; i++) {
        // (uint16_t) cast: for l[i] == -32768 a plain l[i] << 16 overflows
        // signed int, which is UB at -O2. Same bit pattern, defined behaviour.
        out[i * 2 + 0] = (int32_t)((uint32_t)(uint16_t) l[i] << 16);
        out[i * 2 + 1] = (int32_t)((uint32_t)(uint16_t) r[i] << 16);
    }
}

// -----------------------------------------------------------------------------
// Applies one packet from the ring to the engine / FX state. Runs at the top of
// render(), so it must never block.
// -----------------------------------------------------------------------------
static void ipc_apply(uint32_t pkt)
{
    switch (ipc_type(pkt)) {
    case IPC_CMD_NOTE_ON:
        ep.noteOn(ipc_d1(pkt), ipc_d2(pkt));
        break;
    case IPC_CMD_NOTE_OFF:
        ep.noteOff(ipc_d1(pkt));
        break;
    case IPC_CMD_CC:
        ep.processMidiController(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
        break;
    case IPC_CMD_FX_PARAM: {
        float v = ipc_u16_to_f(ipc_d2(pkt));
        switch (ipc_d1(pkt)) {
        case FX_DRIVE:      cp_fx.setDrive(v);         break;
        case FX_TW_DEPTH:   cp_fx.setTremWahDepth(v);  break;
        case FX_TW_RATE:    cp_fx.setTremWahRate(v);   break;
        case FX_CP_DEPTH:   cp_fx.setChoPhaDepth(v);   break;
        case FX_CP_SPEED:   cp_fx.setChoPhaSpeed(v);   break;
        case FX_DLY_DEPTH:  cp_fx.setDelayDepth(v);    break;
        case FX_DLY_TIME:   cp_fx.setDelayTime(v);     break;
        case FX_REVERB:     cp_fx.setReverbDepth(v);   break;
        case FX_VOLUME:     cp_fx.setVolume(v);        break;
        case FX_EXPRESSION: cp_fx.setExpression(v);    break;
        case FX_PRE_GAIN:   cp_fx.setPreGain(v);       break;
        default:                                       break;
        }
    } break;
    case IPC_CMD_FX_MODE: {
        int m = (int) ipc_d2(pkt);
        switch (ipc_d1(pkt)) {
        case FXM_TW_MODE:  cp_fx.setTremWahMode(m); break;
        case FXM_CP_MODE:  cp_fx.setChoPhaMode(m);  break;
        case FXM_DLY_MODE: cp_fx.setDelayMode(m);   break;
        default:                                    break;
        }
    } break;
    case IPC_CMD_VOICE_PARAM:
        ep.setParameter(ipc_d1(pkt), ipc_u16_to_f(ipc_d2(pkt)));
        break;
    case IPC_CMD_PROGRAM:
        preset_apply(ipc_d1(pkt), &ep, &cp_fx);
        break;
    case IPC_CMD_INSTRUMENT:
        ep.setInstrument(ipc_d1(pkt));
        cp_fx.setVoiceType(ep.getCurrentInstrument());
        break;
    case IPC_CMD_PITCH_BEND:
        ep.setPitchBend((int32_t) ipc_d2(pkt));
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// The instrument
// ---------------------------------------------------------------------------

namespace {

class CPInstrument final : public picoface::Instrument {
public:
    CPInstrument() : ui_(ep, cp_fx, refaceMidi) {}

    const char* name() const override { return "PicoFaceCP"; }

    void init() override {
        ep.setVolume(100);
        cp_fx.init((float) SAMPLING_RATE);
        cp_fx.setVoiceType(ep.getCurrentInstrument());
        cp_fx.setVolume(1.0f);
        cp_fx.setDrive(0.0f);
        cp_fx.setChoPhaMode(RefaceCpChain::CP_OFF);
        cp_fx.setChoPhaDepth(0.4f);
        cp_fx.setChoPhaSpeed(0.3f);
        cp_fx.setReverbDepth(0.0f);
        cp_fx.setTremWahMode(RefaceCpChain::TW_OFF);
        cp_fx.setDelayMode(RefaceCpChain::DLY_OFF);
        refaceMidi.init(&ep, &cp_fx);
        // The defaults above act as the fallback when no valid settings record
        // exists. After refaceMidi.init(), because the record also carries the
        // SYSTEM block.
        settings_boot_restore(&ep, &cp_fx, &refaceMidi);
    }

    uint32_t sampleRate() const override { return (uint32_t) SAMPLING_RATE; }

    void render(int32_t* out, uint32_t frames) override {
        // Drain the ring first: MIDI dispatch and panel edits run on this core
        // but outside the producer, so they are applied at block boundaries.
        uint32_t pkt;
        while (cp_ipc_pop(&pkt)) ipc_apply(pkt);
        cp_render_block(out, frames);
    }

    // ----------------------------------------------------------------
    // MIDI - the core parses both wires and dispatches here; the reface
    // layer does channel filtering, transpose and the CC map and pushes
    // onto the ring. The picoface interface passes the channel first,
    // RefaceMidi expects it last.
    // ----------------------------------------------------------------

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override {
        gpio_put(PIN_LED, vel > 0 ? 1 : 0);
        refaceMidi.onNoteOn(note, vel, ch);
    }

    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override {
        gpio_put(PIN_LED, 0);
        refaceMidi.onNoteOff(note, vel, ch);
    }

    void controlChange(uint8_t ch, uint8_t cc, uint8_t v) override { refaceMidi.onControlChange(cc, v, ch); }
    void programChange(uint8_t ch, uint8_t p) override { refaceMidi.onProgramChange(p, ch); }

    // The core hands over -8192..8191, RefaceMidi expects the raw 14-bit value
    // 0..16383 with centre 8192.
    void pitchBend(uint8_t ch, int16_t bend) override { refaceMidi.onPitchBend((uint16_t)((int32_t) bend + 8192), ch); }

    void sysEx(const uint8_t* data, size_t length) override { refaceMidi.onSysEx(data, (uint16_t) length); }

    void realtime(uint8_t status) override { refaceMidi.onRealtime(status); }
    void midiActivity() override { refaceMidi.notifyActivity(); }

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
        refaceMidi.tick();
        settings_task(&ep, &cp_fx, &refaceMidi);
    }

    // settingsSize() stays 0: this instrument writes its own veeprom record
    // from settings_task() rather than handing a buffer to the core.

private:
    CP_Ui ui_;
};

} // namespace

// Register this instrument with the core framework.
PICOFACE_REGISTER_INSTRUMENT(CPInstrument)
