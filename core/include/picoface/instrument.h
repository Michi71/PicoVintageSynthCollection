// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// -----------------------------------------------------------------------------
// picoface/instrument.h
//
// Stable contract between the shared picoface core and a concrete instrument.
//
// The common core (audio pipeline, hardware drivers, GUI framework, USB-MIDI)
// is programmed exclusively against this interface. A new instrument only has
// to:
//
//   1. implement this class in its own instrument subfolder, and
//   2. call PICOFACE_REGISTER_INSTRUMENT(YourInstrumentType) exactly once.
//
// The core never sees the concrete type; it only calls picoface::instrument().
// -----------------------------------------------------------------------------

#ifndef PICOFACE_INSTRUMENT_H
#define PICOFACE_INSTRUMENT_H

#include <cstdint>
#include <cstddef>

#include "picoface/ui.h"
#include "picoface/midi.h"

namespace picoface {

// Abstract base class: the ONLY docking point between the shared core and an
// instrument implementation. Everything below runs on core0 - audio producer,
// USB, MIDI and GUI share that core. core1 belongs to the instrument; it may
// use it as a worker (as PicoFaceRD does) or leave it idle.
class Instrument {
public:
    Instrument() = default;
    virtual ~Instrument() = default;

    // Instruments are singletons owned by the registration macro.
    Instrument(const Instrument&) = delete;
    Instrument& operator=(const Instrument&) = delete;

    // ------------------------------------------------------------------
    // Identity                                                    [core0]
    // ------------------------------------------------------------------

    // Human-readable instrument name (e.g. for UI title / USB descriptors).
    virtual const char* name() const = 0;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    // [core0] Called once after hardware init, before the audio pool is
    // started. The engine determines its own sample rate internally; the
    // core calls init() first and queries sampleRate() afterwards to
    // initialize the audio pool.
    virtual void init() = 0;

    // [core0] Queried by the core after init() to initialize the audio pool.
    // Must return a valid sample rate after init() has been called.
    virtual uint32_t sampleRate() const = 0;

    // ------------------------------------------------------------------
    // Audio pipeline              [audio producer context, hard realtime]
    // ------------------------------------------------------------------

    // Renders one block of audio into 'out'.
    // 'out' holds TWO int32 words per frame - left then right - each carrying
    // its 16-bit sample in the upper half (value << 16), matching the buffer
    // layout of the pico-extras audio pool and the existing engine method
    // fill_buffer_i32(). 'frames' is the frame count, so the callee writes
    // 2 * frames words. (This comment used to say "one int32 word per frame,
    // packed stereo", which is not what any instrument does or what the PIO
    // reads.)
    // Called BLOCK-WISE from the core's producer loop - currently on
    // core0; an instrument may internally use core1 as a worker.
    // Hard realtime constraints: must not block, must not allocate,
    // no printf.
    virtual void render(int32_t* out, uint32_t frames) = 0;

    // ------------------------------------------------------------------
    // Audio pipeline hooks              [audio producer context]
    // Optional; the defaults make the core behave exactly as before, so
    // instruments that do not need them override nothing.
    // ------------------------------------------------------------------

    // Polled by the core after every rendered block. Return true exactly once
    // when the engine has switched its internal sample rate; the core then
    // re-reads sampleRate() and applies it to the hardware only after the
    // buffers already queued in the DMA pipeline have drained - switching
    // immediately would play the old-rate tail at the wrong speed. Consuming
    // semantics: the flag must clear on read.
    virtual bool consumeSampleRateChange() { return false; }

    // Called when the I2S underrun counter has increased since the last
    // main-loop pass. An instrument can use this to shed load, for example by
    // dropping voices.
    virtual void onAudioUnderrun() {}

    // Asked before the core writes the debounced settings record. Return false
    // to postpone: a flash write stalls the CPU for milliseconds, which is
    // audible while notes are sounding. The core retries on the next idle
    // window.
    virtual bool settingsSaveAllowed() const { return true; }

    // ------------------------------------------------------------------
    // MIDI                                                        [core0]
    // ------------------------------------------------------------------
    // The core parses USB-MIDI (and DIN-MIDI) and dispatches here.
    // Implementations must forward events to the audio core via a
    // lock-free queue if they affect the render path.

    virtual void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) = 0;
    virtual void noteOff(uint8_t channel, uint8_t note, uint8_t velocity) = 0;
    virtual void controlChange(uint8_t channel, uint8_t cc, uint8_t value) {}
    virtual void programChange(uint8_t channel, uint8_t program) {}
    virtual void pitchBend(uint8_t channel, int16_t value) {}
    virtual void sysEx(const uint8_t* data, size_t length) {}

    // Realtime status bytes (0xF8..0xFF) from either wire. Needed by an
    // instrument that supervises active sensing (0xFE); the default ignores
    // them.
    virtual void realtime(uint8_t status) {}

    // Any MIDI traffic on either wire, reported before the message itself is
    // dispatched. Only useful together with realtime(): it is what keeps an
    // active-sensing timeout from firing during a dense stream. The default
    // ignores it.
    virtual void midiActivity() {}

    // ------------------------------------------------------------------
    // GUI                                                         [core0]
    // ------------------------------------------------------------------

    // Called once after display init; register screens/widgets here.
    virtual void uiInit(ui::Display& display) {}

    // Called from the main loop, typically at 30-60 Hz. Draw UI and
    // consume input events. Must stay non-blocking.
    virtual void uiTick(ui::Display& display, const ui::InputState& input) {}

    // ------------------------------------------------------------------
    // Persistence                                                 [core0]
    // ------------------------------------------------------------------
    // Settings are stored by the core (flash/SD). The instrument only
    // serializes its own state into the provided buffer.

    // Version of the instrument's own payload format. The core writes
    // this value into the veeprom record and discards on load any
    // record whose version differs. Increment on every layout change.
    virtual uint16_t settingsVersion() const { return 1; }

    // Number of bytes the instrument needs for its settings. 0 = no state.
    virtual size_t settingsSize() const { return 0; }

    // Serialize settings into 'buffer' (capacity 'size', == settingsSize()).
    virtual void settingsSave(uint8_t* buffer, size_t size) const {}

    // Deserialize settings from 'buffer' ('size' bytes stored previously).
    // Must tolerate unknown/short buffers gracefully (version your format).
    virtual void settingsLoad(const uint8_t* buffer, size_t size) {}
};

// The single entry point used by the core to reach the instrument.
// Exactly ONE definition must exist per firmware image, provided by the
// instrument subfolder (normally via PICOFACE_REGISTER_INSTRUMENT).
// The core only ever calls this function.
Instrument& instrument();

} // namespace picoface

// Registers an instrument type for this firmware image. TYPE must derive
// from picoface::Instrument and be default-constructible. Expands to the
// required definition of picoface::instrument() backed by a function-local
// static instance (lazy construction, no static-init-order issues).
// Use exactly once, at global scope, in the instrument subfolder.
//
// NOTE: the first call must happen on core0 (the core calls init() there
// before starting core1), so the lazy-init guard is never contended.
#define PICOFACE_REGISTER_INSTRUMENT(TYPE)                \
    namespace picoface {                                  \
        Instrument& instrument() {                        \
            static TYPE instance;                         \
            return instance;                              \
        }                                                 \
    }

#endif // PICOFACE_INSTRUMENT_H
