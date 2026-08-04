// CP_Instrument.cpp - adapter binding the Yamaha reface CP electric piano
// (mdaEPiano engine plus the reface CP effect chain) to picoface::Instrument.
// Like PicoFaceYC it uses the alternative runtime model: core0 is a pure
// audio master, core1 owns USB, MIDI, the encoders and the front panel menu.

#include <cstdio>
#include <cmath>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "picoface/instrument.h"
#include "project_config.h"
#include "ipc.h"
#include "midi_input_usb.h"
#include "midi_reface.h"
#include "mdaEPiano.h"
#include "presets.h"
#include "audio_subsystem.h"
#include "pico_hw.h"
#include "get_serial.h"
#include "u8g2.h"
#include "encoder.h"
#include "push_button.h"
#include "pico_userinterface.h"
#include "pico_frontpanel.h"
#include "settings.h"
#include "veeprom.h"
#include "cp_logo.h"
#include "reface_cp_chain.h"
#include "cp_audio.h"        // cp_process_block_i16()

#if __has_include("bsp/board_api.h")
#include "bsp/board_api.h"
#else
#include "bsp/board.h"
#endif

// ---------------------------------------------------------------------------
// File-level objects
// ---------------------------------------------------------------------------
Encoder encSel(pio1, 0, {PIN_SEL_CLK, PIN_SEL_DT});
Encoder encA(pio1, 1, {PIN_PA_CLK, PIN_PA_DT});
Encoder encB(pio1, 2, {PIN_PB_CLK, PIN_PB_DT});
PushButton btSel(PIN_SEL_SW, 50);
PushButton btA(PIN_PA_SW, 50);
PushButton btB(PIN_PB_SW, 50);
static u8g2_t u8g2;
static mdaEPiano ep(96);
static RefaceCpChain cp_fx;          // reface CP effect chain, post-processes the engine output
static MIDIInputUSB usbmidi;         // core1
static volatile int g_octave = 0;
// External linkage on purpose: pico_frontpanel.cpp declares 'extern RefaceMidi refaceMidi;'
RefaceMidi refaceMidi;               // core1: channel filter, CC map, SysEx, active sensing

extern "C" {

void ui_set_octave(int oct) {
    if (oct < -2) oct = -2;
    if (oct >  2) oct =  2;
    g_octave = oct;
}

int ui_get_octave(void) {
    return g_octave;
}

void ui_poll_usb(void) {
    tud_task();
    usbmidi.process();
    refaceMidi.tick();
    settings_task(&ep, &cp_fx, &refaceMidi);   // debounced autosave to the virtual EEPROM
}

void ui_wait_button_release(PushButton* bt) {
    absolute_time_t cap = make_timeout_time_ms(3000);
    while (bt->ReadButton() == PushButton::PRESSED && !time_reached(cap)) {
        ui_poll_usb();
        sleep_ms(1);
    }
}

} // extern "C"

// ---------------------------------------------------------------------------
// MIDI callbacks
// ---------------------------------------------------------------------------
static void note_on_callback(uint8_t note, uint8_t level, uint8_t channel) {
    gpio_put(PIN_LED, level > 0 ? 1 : 0);
    refaceMidi.onNoteOn(note, level, channel);
}

static void note_off_callback(uint8_t note, uint8_t level, uint8_t channel) {
    gpio_put(PIN_LED, 0);
    refaceMidi.onNoteOff(note, level, channel);
}

static void cc_callback(uint8_t cc, uint8_t value, uint8_t channel) {
    refaceMidi.onControlChange(cc, value, channel);
}

static void program_change_callback(uint8_t program, uint8_t channel) {
    refaceMidi.onProgramChange(program, channel);
}

static void pitch_bend_callback(uint16_t b14, uint8_t channel) {
    refaceMidi.onPitchBend(b14, channel);
}

static void realtime_callback(uint8_t s) {
    refaceMidi.onRealtime(s);
}

static void sysex_callback(const uint8_t* d, uint16_t len) {
    refaceMidi.onSysEx(d, len);
}

static void activity_callback(void) {
    refaceMidi.notifyActivity();
}

// -----------------------------------------------------------------------------
// Flash park handshake
// -----------------------------------------------------------------------------
// Mirrors the YC pattern: before core1 erases or programs flash it asks core0
// to park itself in a RAM-resident loop, because XIP from flash is unavailable
// for the duration of the operation.

static volatile uint32_t g_flash_park_ack = 0;
static volatile uint32_t g_flash_release  = 0;

// flash_park_core0: Core0, called from ipc_apply in the audio producer loop.
// Must be RAM resident because XIP is unavailable while core1 erases or
// programs flash, and guaranteed out of line: plain __not_in_flash_func does
// not stop the compiler from inlining this static function into a
// flash-resident caller. 2 s timeout as a worst-case erase+program guard.
// NOTE: no watchdog_update() here -- CP has no watchdog.
static void __no_inline_not_in_flash_func(flash_park_core0)(void)
{
    uint32_t ints = save_and_disable_interrupts();
    g_flash_park_ack = 1;
    uint32_t start = time_us_32();
    while (!g_flash_release && (time_us_32() - start) < 2000000u) {
        tight_loop_contents();
    }
    g_flash_park_ack = 0;
    restore_interrupts(ints);
}

// Core1 side: request the park, wait up to 100 ms for the ack.
static bool flash_lock_core1(void)
{
    g_flash_release = 0;
    ipc_send_flash_lock();
    uint32_t start = time_us_32();
    while (!g_flash_park_ack) {
        if ((time_us_32() - start) > 100000u) return false;
        tight_loop_contents();
    }
    return true;
}

// Core1 side: release core0, then wait briefly until it has left the park loop.
static void flash_unlock_core1(void)
{
    g_flash_release = 1;
    uint32_t start = time_us_32();
    while (g_flash_park_ack && (time_us_32() - start) < 10000u) {
        tight_loop_contents();
    }
}

// -----------------------------------------------------------------------------
// ipc_apply (core0)
// -----------------------------------------------------------------------------
// Applies one IPC packet received from core1 to the engine / FX state.
// Called from the audio producer loop, so it must never block.
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
    case IPC_CMD_FLASH_LOCK:
        flash_park_core0();
        break;
    default:
        break;
    }
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

// ---------------------------------------------------------------------------
// The instrument
// ---------------------------------------------------------------------------

namespace {

class CPInstrument final : public picoface::Instrument {
public:

    const char* name() const override { return "PicoFaceCP"; }

    // core1 runs USB, MIDI, encoders and the front panel menu
    bool ownsUserInterface() const override { return true; }

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
        // The defaults above act as the fallback when no valid settings record exists.
        // Single-core phase: plain XIP reads, direct setters.
        settings_boot_restore_core0(&ep, &cp_fx);
    }

    uint32_t sampleRate() const override { return (uint32_t) SAMPLING_RATE; }

    void render(int32_t* out, uint32_t frames) override { cp_render_block(out, frames); }

    void pumpCrossCore() override {
        // Apply pending core1 -> core0 commands, so a note-on, parameter edit or
        // program change lands on the very next block rendered. Also the entry
        // point of the flash-park handshake (IPC_CMD_FLASH_LOCK).
        while (multicore_fifo_rvalid()) ipc_apply(multicore_fifo_pop_blocking());
    }

    FlashLockFn flashLockHook() const override { return flash_lock_core1; }
    FlashUnlockFn flashUnlockHook() const override { return flash_unlock_core1; }

    // MIDI: never called - the core does not wire USB MIDI when the instrument owns
    // the user interface; CP receives on core1 through its own callbacks.
    void noteOn(uint8_t, uint8_t, uint8_t) override {}
    void noteOff(uint8_t, uint8_t, uint8_t) override {}

    void runUserInterface() override {
        // FPSCR is per-core: without this, any float use on this core (u8g2 layout
        // maths, ipc_f_to_u16(), the settings snapshot) reintroduces the denormal
        // slow path that pico_init() guards core0 against.
        pico_fpu_ftz_enable();
        board_init(); usb_serial_init(); tusb_init();
        btSel.Init(); btA.Init(); btB.Init();
        encSel.init(); encA.init(); encB.init();
        u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
        u8g2_InitDisplay(&u8g2); u8g2_SetPowerSave(&u8g2, 0); u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_8x13B_tf); u8g2_SetBitmapMode(&u8g2, false);
        u8g2_FirstPage(&u8g2); do { u8g2_DrawXBMP(&u8g2, 0, 0, logoWidth, logoHeight, logo); } while (u8g2_NextPage(&u8g2));
        // keep USB enumeration alive during the splash screen
        absolute_time_t splash_end = make_timeout_time_ms(2000);
        while (!time_reached(splash_end)) { tud_task(); sleep_ms(1); }
        usbmidi.setCCCallback(cc_callback); usbmidi.setProgramChangeCallback(program_change_callback);
        usbmidi.setNoteOnCallback(note_on_callback); usbmidi.setNoteOffCallback(note_off_callback);
        usbmidi.setPitchBendCallback(pitch_bend_callback); usbmidi.setRealtimeCallback(realtime_callback);
        usbmidi.setSysExCallback(sysex_callback); usbmidi.setActivityCallback(activity_callback);
        refaceMidi.init(&ep, &cp_fx);
        settings_boot_restore_core1(&refaceMidi);   // restore octave + MIDI SYSTEM block
        while (true) {
            ui_poll_usb();
            static bool cleared = false;
            if (!cleared) { u8g2_ClearDisplay(&u8g2); u8g2_SetDrawColor(&u8g2, 1); cleared = true; }
            pico_UserInterfaceFrontPanel(&u8g2, &encSel, &btSel, &encA, &btA, &encB, &btB, &ep, &cp_fx);
        }
    }

};

} // namespace

// Register this instrument with the core framework.
PICOFACE_REGISTER_INSTRUMENT(CPInstrument)
