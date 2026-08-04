// YC_Instrument.cpp - adapter binding the Yamaha reface YC organ to picoface::Instrument.
// YC uses the alternative runtime model: core0 is a pure audio master, core1 owns USB,
// MIDI, the encoders and the whole front panel menu. The objects below stay at file
// scope because the shared ui_panel module reaches back into them through plain
// function pointers.

#include <cstdio>
#include <cmath>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "picoface/instrument.h"
#include "project_config.h"
#include "ipc.h"
#include "midi_input_usb.h"
#include "midi_serial.h"
#include "midi_reface.h"
#include "YC_Synth_Bridge.h"
#include "YC_Controller.h"
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
#include "yc_logo.h"

#if __has_include("bsp/board_api.h")
#include "bsp/board_api.h"
#else
#include "bsp/board.h"
#endif

// ---------------------------------------------------------------------------
// File-scope objects
// ---------------------------------------------------------------------------

Encoder encSel(pio1, 0, {PIN_SEL_CLK, PIN_SEL_DT});
Encoder encA(pio1, 1, {PIN_PA_CLK, PIN_PA_DT});
Encoder encB(pio1, 2, {PIN_PB_CLK, PIN_PB_DT});
PushButton btSel(PIN_SEL_SW, 50);
PushButton btA(PIN_PA_SW, 50);
PushButton btB(PIN_PB_SW, 50);

static u8g2_t u8g2;
static MIDIInputUSB usbmidi;          // core1
static RefaceMidi refaceMidi;         // core1: channel/CC/SysEx/active sensing
static volatile int g_octave = 0;     // octave transpose applied to incoming notes

// External linkage on purpose: pico_frontpanel.cpp declares both as extern and
// drives them directly (see the "extern YC_Controller ycController;" pair there).
YC_Synth_Bridge ycBridge;             // core0 owned, the sole synth engine
YC_Controller ycController(ycBridge, refaceMidi);   // core1, mutates through IPC

// ---------------------------------------------------------------------------
// ui_panel hooks (the ui_panel module expects them with C linkage)
// ---------------------------------------------------------------------------

extern "C" void ui_set_octave(int oct) {
    if (oct < -2) oct = -2;
    if (oct >  2) oct =  2;
    g_octave = oct;
}

extern "C" int ui_get_octave(void) {
    return g_octave;
}

// core1 periodic service - also pumped from blocking UI wait loops so USB
// keeps running while a menu is open
extern "C" void ui_poll_usb(void) {
    tud_task();
    usbmidi.process();
    // DIN MIDI is serviced here too: this instrument owns MIDI on core1,
    // so the core never polls it for us.
    midiSerial().process();
    refaceMidi.tick();
    settings_task(&ycBridge, &refaceMidi);
}

// block (pumping USB) until the encoder button is released, so a single click
// is not consumed by several menu screens in a row - ReadButton is level based
extern "C" void ui_wait_button_release(PushButton* bt) {
    absolute_time_t cap = make_timeout_time_ms(3000);
    while (bt->ReadButton() == PushButton::PRESSED && !time_reached(cap)) {
        ui_poll_usb();
        sleep_ms(1);
    }
}

// ---------------------------------------------------------------------------
// MIDI callbacks
// Run on core1 (the MIDI/USB task), forward over the FIFO to core0.
// ---------------------------------------------------------------------------
static void note_on_callback(uint8_t note, uint8_t level, uint8_t channel)
{
    gpio_put(PIN_LED, level > 0 ? 1 : 0);
    refaceMidi.onNoteOn(note, level, channel);
}

static void note_off_callback(uint8_t note, uint8_t level, uint8_t channel)
{
    gpio_put(PIN_LED, 0);
    refaceMidi.onNoteOff(note, level, channel);
}

static void cc_callback(uint8_t cc, uint8_t value, uint8_t channel)
{
    refaceMidi.onControlChange(cc, value, channel);
}

// The YC has no program change per its MIDI implementation chart.
static void program_change_callback(uint8_t program, uint8_t channel)
{
}

static void pitch_bend_callback(uint16_t b14, uint8_t channel)
{
    refaceMidi.onPitchBend(b14, channel);
}

static void realtime_callback(uint8_t s)
{
    refaceMidi.onRealtime(s);
}

static void sysex_callback(const uint8_t* d, uint16_t len)
{
    refaceMidi.onSysEx(d, len);
}

static void activity_callback(void)
{
    refaceMidi.notifyActivity();
}

// ---------------------------------------------------------------------------
// Flash park handshake
// ---------------------------------------------------------------------------
static volatile uint32_t g_flash_park_ack = 0;
static volatile uint32_t g_flash_release  = 0;

// Core0, called from ipc_apply() in the audio producer loop. Must be RAM
// resident because XIP is unavailable while core1 erases or programs flash -
// and guaranteed out of line: plain __not_in_flash_func does not stop the
// compiler from inlining this static function into a flash-resident caller.
// 2 s timeout as a worst-case erase+program guard.
static void __no_inline_not_in_flash_func(flash_park_core0)(void)
{
    uint32_t ints = save_and_disable_interrupts();
    g_flash_park_ack = 1;
    uint32_t start = time_us_32();
    while (!g_flash_release && (time_us_32() - start) < 2000000u)
    {
        tight_loop_contents();
        watchdog_update();
    }
    g_flash_park_ack = 0;
    restore_interrupts(ints);
}

// Core1 veeprom lock hook: request the park, wait at most 100 ms for the ack
// (the audio producer drains the FIFO continuously, woken by every FIFO push).
// false = abort the save, flash untouched.
static bool flash_lock_core1(void)
{
    g_flash_release = 0;
    ipc_send_flash_lock();
    uint32_t start = time_us_32();
    while (!g_flash_park_ack)
    {
        if ((time_us_32() - start) > 100000u)
            return false;
        tight_loop_contents();
    }
    return true;
}

// Core1 veeprom unlock hook: release core0, wait briefly until it left the
// spin loop.
static void flash_unlock_core1(void)
{
    g_flash_release = 1;
    uint32_t start = time_us_32();
    while (g_flash_park_ack && (time_us_32() - start) < 10000u)
    {
        tight_loop_contents();
    }
}

// ---------------------------------------------------------------------------
// ipc_apply - runs on core0
// ---------------------------------------------------------------------------
static void ipc_apply(uint32_t pkt)
{
    switch (ipc_type(pkt))
    {
    case IPC_CMD_YC_NOTE_ON:
        ycBridge.noteOn(ipc_d1(pkt), (uint8_t) ipc_d2(pkt));
        break;
    case IPC_CMD_YC_NOTE_OFF:
        ycBridge.noteOff(ipc_d1(pkt));
        break;
    case IPC_CMD_YC_PANEL_UPDATE:
        ycBridge.setParam(ipc_d1(pkt), ipc_d2(pkt));
        break;
    case IPC_CMD_YC_SUSTAIN:
        ycBridge.setSustain(ipc_d1(pkt) != 0);
        break;
    case IPC_CMD_YC_ALL_NOTES_OFF:
        ycBridge.allNotesOff();
        break;
    case IPC_CMD_YC_ROTARY_TARGET:
        ycBridge.setRotaryTarget(ipc_d1(pkt));
        break;
    case IPC_CMD_YC_MIDI_CTRL_MODE:
        break;  // not implemented yet
    case IPC_CMD_YC_PITCH_BEND:
        break;  // pitch bend not implemented in the tonegen yet
    case IPC_CMD_FLASH_LOCK:
        flash_park_core0();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// The instrument
// ---------------------------------------------------------------------------

namespace {

class YCInstrument final : public picoface::Instrument {
public:

    const char* name() const override { return "PicoFaceYC"; }

    // --- alternative runtime model ------------------------------------
    // core1 runs USB, MIDI, encoders and the front panel menu
    bool ownsUserInterface() const override { return true; }

    void init() override {
        // Count watchdog-caused reboots in a scratch register that survives them.
        if (watchdog_caused_reboot()) { watchdog_hw->scratch[0] += 1; } else { watchdog_hw->scratch[0] = 0; }
        ycBridge.init();
        // Defaults in the engine act as the fallback when no valid record exists.
        settings_boot_restore_core0(&ycBridge);
    }

    uint32_t sampleRate() const override { return (uint32_t) YC_SAMPLE_RATE; }

    void render(int32_t* out, uint32_t frames) override {
        // The YC engine renders float; convert to the packed int32 the I2S pool wants.
        static float buf[SAMPLES_PER_BUFFER * 2];
        uint32_t n = frames;
        // The pool never hands out more than SAMPLES_PER_BUFFER; this only guards the fixed buffer.
        if (n > SAMPLES_PER_BUFFER) n = SAMPLES_PER_BUFFER;
        ycBridge.fill_buffer(buf, (int) n);
        for (uint32_t i = 0; i < n; ++i) {
            int32_t dl = (int32_t)(buf[i * 2 + 0] * 32767.0f);
            int32_t dr = (int32_t)(buf[i * 2 + 1] * 32767.0f);
            if (dl < -32768) dl = -32768; else if (dl > 32767) dl = 32767;
            if (dr < -32768) dr = -32768; else if (dr > 32767) dr = 32767;
            out[i * 2 + 0] = dl << 16;
            out[i * 2 + 1] = dr << 16;
        }
    }

    void pumpCrossCore() override {
        // The watchdog can only be armed once audio is running; the first pass through
        // the producer loop is exactly that moment.
        if (!watchdogArmed_) { watchdog_enable(4000, false); watchdogArmed_ = true; }
        watchdog_update();
        // Apply pending core1 -> core0 commands, so a parameter edit, note-on or panel
        // change lands on the very next block. Also the entry point of the flash-park
        // handshake (IPC_CMD_FLASH_LOCK).
        while (multicore_fifo_rvalid()) ipc_apply(multicore_fifo_pop_blocking());
    }

    FlashLockFn flashLockHook() const override { return flash_lock_core1; }
    FlashUnlockFn flashUnlockHook() const override { return flash_unlock_core1; }

    // --- MIDI: never called. The core does not wire USB MIDI when the instrument
    // --- owns the user interface; YC receives on core1 through its own callbacks.
    void noteOn(uint8_t, uint8_t, uint8_t) override {}
    void noteOff(uint8_t, uint8_t, uint8_t) override {}

    void runUserInterface() override {
        pico_fpu_ftz_enable();   // FPSCR is per-core state; this core has float paths too
        board_init(); usb_serial_init(); tusb_init();
        btSel.Init(); btA.Init(); btB.Init();
        encSel.init(); encA.init(); encB.init();
        u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
        u8g2_InitDisplay(&u8g2); u8g2_SetPowerSave(&u8g2, 0); u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_8x13B_tf); u8g2_SetBitmapMode(&u8g2, false);
        u8g2_FirstPage(&u8g2); do { u8g2_DrawXBMP(&u8g2, 0, 0, logoWidth, logoHeight, logo); } while (u8g2_NextPage(&u8g2));
        // Keep USB enumeration alive during the splash screen.
        absolute_time_t splash_end = make_timeout_time_ms(2000);
        while (!time_reached(splash_end)) { tud_task(); sleep_ms(1); }
        usbmidi.setCCCallback(cc_callback); usbmidi.setProgramChangeCallback(program_change_callback);
        usbmidi.setNoteOnCallback(note_on_callback); usbmidi.setNoteOffCallback(note_off_callback);
        usbmidi.setPitchBendCallback(pitch_bend_callback); usbmidi.setRealtimeCallback(realtime_callback);
        usbmidi.setSysExCallback(sysex_callback); usbmidi.setActivityCallback(activity_callback);
        // Same handlers for DIN MIDI, so both wires feed the identical path.
        midiSerial().init();
        midiSerial().setCCCallback(cc_callback);
        midiSerial().setProgramChangeCallback(program_change_callback);
        midiSerial().setNoteOnCallback(note_on_callback);
        midiSerial().setNoteOffCallback(note_off_callback);
        midiSerial().setPitchBendCallback(pitch_bend_callback);
        midiSerial().setRealtimeCallback(realtime_callback);
        midiSerial().setSysExCallback(sysex_callback);
        midiSerial().setActivityCallback(activity_callback);
        refaceMidi.init(&ycBridge);
        settings_boot_restore_core1(&refaceMidi);
        while (true) {
            ui_poll_usb();
            // Draw one front panel frame.
            static bool cleared = false;
            if (!cleared) { u8g2_ClearDisplay(&u8g2); u8g2_SetDrawColor(&u8g2, 1); cleared = true; }
            pico_UserInterfaceFrontPanel(&u8g2, &encSel, &btSel, &encA, &btA, &encB, &btB);
        }
    }

private:
    bool watchdogArmed_ = false;
};

} // namespace

PICOFACE_REGISTER_INSTRUMENT(YCInstrument)
