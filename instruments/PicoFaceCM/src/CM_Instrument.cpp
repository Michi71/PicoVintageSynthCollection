// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// PicoFaceCM - Roland CM-32L.
//
// The engine is mt32emu's synthesis core (../mt32emu), driven with the
// original control ROM and the PCM ROM read in place from flash. Both cores
// render: they share the generation of the partials between them, then core1
// runs the reverb while core0 mixes the rest. The engine's README says why
// the output is the same either way.
//
// MIDI is passed to the engine as it arrives: parts 1-8 on channels 2-9 and
// rhythm on channel 10 after power-up, as on the original, unless the panel
// or the music says otherwise.
//
// The panel is CM_Controller: every value on it is a byte in the module's
// memory. This file adds the two pages only the engine can fill - what the
// module's display would show, and the developer's numbers.
#include <atomic>
#include <cstdio>
#include <cstring>

#include "pico/multicore.h"
#include "pico/time.h"

#include "picoface/instrument.h"
#include "picoface/ui_kit.h"
#include "audio_i2s.h"          // g_i2s_underrun_count

#include "File.h"
#include "ROMInfo.h"
#include "Synth.h"

#include "CM_Controller.h"
#include "CM_SynthModule.h"
#include "CM_Ui.h"
#include "cm_settings.h"

extern "C" {
extern const uint8_t cm_control_rom[];
extern const uint8_t cm_control_rom_end[];
extern const int16_t cm_pcm_words[];
extern const int16_t cm_pcm_words_end[];
}

namespace {

constexpr uint32_t kSampleRate = 32000;     // the LA32's own rate
constexpr uint32_t kChunk = 64;             // frames rendered per engine call
constexpr int kTestNotesMax = 16;
// For the diagnostics page: spread over the keyboard, on whatever part the panel has selected.
constexpr uint8_t kTestNotes[kTestNotesMax] = {48, 55, 60, 64, 67, 72, 76, 79,
                                               43, 52, 59, 62, 69, 74, 81, 84};

// core1 as the engine's second executor. It sleeps in WFE until core0 forks,
// runs its lane and reports back; core0 runs the other lane meanwhile and then
// spins for the remainder, which is short when the work was shared evenly.
//
// The loop itself lives in RAM, so core1 does not fetch from flash while it
// idles - whatever core0 does with the flash in between.
class Core1Helper final : public MT32Emu::ParallelHelper {
public:
    void start() {
        instance_ = this;
        multicore_reset_core1();
        multicore_launch_core1(entry);
    }

    void fork(void (*work)(void*, int), void* context, int stage) override {
        const uint32_t t0 = time_us_32();
        work_ = work;
        context_ = context;
        const uint32_t seq = request_.load(std::memory_order_relaxed) + 1;
        request_.store(seq, std::memory_order_release);
        __sev();
        work(context, 0);
        while (done_.load(std::memory_order_acquire) != seq) tight_loop_contents();
        forkUs_[stage & 1] += time_us_32() - t0;     // 0: generation, 1: mix and reverb
    }

    // Time spent in the two forks since the last call, in microseconds.
    void takeTimes(uint32_t& generateUs, uint32_t& mixUs) {
        generateUs = forkUs_[0];
        mixUs = forkUs_[1];
        forkUs_[0] = forkUs_[1] = 0;
    }

private:
    static void __not_in_flash_func(entry)() {
        Core1Helper& h = *instance_;
        uint32_t served = 0;
        for (;;) {
            uint32_t seq;
            while ((seq = h.request_.load(std::memory_order_acquire)) == served) __wfe();
            h.work_(h.context_, 1);
            served = seq;
            h.done_.store(seq, std::memory_order_release);
        }
    }

    static inline Core1Helper* instance_ = nullptr;
    void (*work_)(void*, int) = nullptr;
    void* context_ = nullptr;
    std::atomic<uint32_t> request_{0}, done_{0};
    uint32_t forkUs_[2] = {0, 0};
};

class CMInstrument final : public picoface::Instrument {
public:
    const char* name() const override { return "PicoFaceCM"; }

    void init() override {
        static MT32Emu::ArrayFile controlFile(
            cm_control_rom, size_t(cm_control_rom_end - cm_control_rom));
        const MT32Emu::ROMImage* control = MT32Emu::ROMImage::makeROMImage(&controlFile);
        synth_.setExternalPCMData(cm_pcm_words, size_t(cm_pcm_words_end - cm_pcm_words));
        synth_.selectRendererType(MT32Emu::RendererType_BIT16S);
        // Two executors need the pitch timers' jitter per partial, see Synth.h.
        synth_.setPerPartialPitchJitter(true);
        // A note that takes partials from a sounding one: render the fade-out in one run.
        // Sample by sample, as upstream does it, costs more than a block has.
        synth_.setChunkedAbortRuns(true);
        // MIDI goes through the engine's queue, due at once. The queue is what keeps a note
        // that has to wait for partials; SysEx gets a fixed buffer there instead of the heap.
        synth_.setJobClock([]() -> MT32Emu::Bit32u { return time_us_32(); });
        synth_.setMIDIDelayMode(MT32Emu::MIDIDelayMode_IMMEDIATE);
        synth_.configureMIDIEventQueueSysexStorage(4096);
        core1_.start();
        synth_.setParallelHelper(&core1_);
        // The PCM image argument is ignored once external PCM data is set.
        ok_ = synth_.open(*control, *control, MT32Emu::DEFAULT_MAX_PARTIALS,
                          MT32Emu::AnalogOutputMode_DIGITAL_ONLY);
        if (ok_) controller_.begin();
    }

    uint32_t sampleRate() const override { return kSampleRate; }

    void render(int32_t* out, uint32_t frames) override {
        if (!ok_) {
            memset(out, 0, 2 * frames * sizeof(int32_t));
            return;
        }
        const uint32_t t0 = time_us_32();
        const uint32_t total = frames;
        int16_t buf[2 * kChunk];
        while (frames) {
            const uint32_t n = frames < kChunk ? frames : kChunk;
            synth_.render(buf, n);
            for (uint32_t i = 0; i < 2 * n; ++i) *out++ = int32_t(buf[i]) << 16;
            frames -= n;
        }
        // Render time against the time the block lasts, in tenths of a percent.
        const uint32_t spent = time_us_32() - t0;
        const uint32_t load = uint32_t(uint64_t(spent) * kSampleRate / (uint64_t(total) * 1000u));
        loadAvg_ += (int32_t(load) - int32_t(loadAvg_)) / 16;
        uint32_t genUs, mixUs;
        core1_.takeTimes(genUs, mixUs);
        const uint32_t gen = uint32_t(uint64_t(genUs) * kSampleRate / (uint64_t(total) * 1000u));
        const uint32_t mix = uint32_t(uint64_t(mixUs) * kSampleRate / (uint64_t(total) * 1000u));
        genAvg_ += (int32_t(gen) - int32_t(genAvg_)) / 16;
        mixAvg_ += (int32_t(mix) - int32_t(mixAvg_)) / 16;
        // The worst block keeps what it consisted of: a peak reading alone does not say
        // whether it was partials, the reverb, or a burst of MIDI events.
        const uint32_t runs = synth_.getRenderRunCount();
        eventsQueued_ += module_.takeQueued();
        if (load > worst_.load) {
            worst_.load = load;
            worst_.gen = gen;
            worst_.mix = mix;
            worst_.events = eventsQueued_;
            worst_.runs = runs - runsSeen_;
        }
        runsSeen_ = runs;
        eventsQueued_ = 0;
        // What a partial costs to generate, by kind: sums over about a second, then shown.
        MT32Emu::Bit32u jt[4];
        synth_.takeJobTimes(jt);
        for (int i = 0; i < 4; ++i) jobSums_[i] += jt[i];
        if (jobSums_[2] + jobSums_[3] > 32u * kSampleRate / 2) {
            synthNs_ = jobSums_[2] ? uint32_t(uint64_t(jobSums_[0]) * 1000u / jobSums_[2]) : 0;
            pcmNs_ = jobSums_[3] ? uint32_t(uint64_t(jobSums_[1]) * 1000u / jobSums_[3]) : 0;
            jobSums_[0] = jobSums_[1] = jobSums_[2] = jobSums_[3] = 0;
        }
    }

    // ---------------------------------------------------------------- MIDI
    void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override { play(0x90, ch, note, vel); }
    void noteOff(uint8_t ch, uint8_t note, uint8_t vel) override { play(0x80, ch, note, vel); }
    void controlChange(uint8_t ch, uint8_t cc, uint8_t value) override { play(0xB0, ch, cc, value); }
    void programChange(uint8_t ch, uint8_t program) override {
        play(0xC0, ch, program, 0);
        // The module's memory holds the timbre, not the program number that chose it.
        if (ok_) controller_.midiProgramChange(ch & 0x0F, program);
        dirty_ = true;
    }
    void pitchBend(uint8_t ch, int16_t value) override {
        const uint16_t v = uint16_t(value + 8192);
        play(0xE0, ch, uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F));
    }
    void sysEx(const uint8_t* data, size_t length) override {
        if (!ok_ || length == 0) return;
        // The queue takes complete messages only.
        if (data[0] != 0xF0 || data[length - 1] != 0xF7) return;
        if (synth_.playSysex(data, MT32Emu::Bit32u(length))) ++eventsQueued_;
        // A data set to 7F 00 00 resets the module, and with it the programs of the parts.
        if (length >= 8 && data[1] == 0x41 && data[3] == 0x16 && data[4] == 0x12 && data[5] == 0x7F)
            controller_.midiReset();
        dirty_ = true;
    }

    // ------------------------------------------------------------------ UI
    void uiInit(picoface::ui::Display&) override {}

    void uiTick(picoface::ui::Display& d, const picoface::ui::InputState& in) override {
        using picoface::ui::Button;
        using picoface::ui::Encoder;
        namespace kit = picoface::ui::kit;

        if (!ok_) {
            if (in.nowMs - lastDrawMs_ < 500u) return;
            lastDrawMs_ = in.nowMs;
            const char* rows[2] = {"engine did not open", "check the ROM set"};
            kit::diagnostics(d, "CM-32L", rows, 2);
            d.flush();
            return;
        }

        const int8_t ds = in.delta(Encoder::Sel);
        const int8_t da = in.delta(Encoder::ParamA);
        const int8_t db = in.delta(Encoder::ParamB);

        // A long press on the select encoder: the module as it is after power-up, with the
        // panel's own settings. The way out of whatever a MIDI file left behind.
        if (in.longPress(Button::Sel)) {
            testNotes_ = 0;
            controller_.reset();
            popupUntilMs_ = in.nowMs + 1000u;
            kit::popup(d, "Reset", "Power-up state");
            d.flush();
            marquee_ = false;
            dirty_ = true;
            return;
        }

        if (ds) { controller_.onEncoderSel(ds); dirty_ = true; }
        if (da) {
            if (controller_.isDiagPage()) holdTestNotes(testNotes_ + da);
            else controller_.onEncoderA(da);
            dirty_ = true;
        }
        if (db) { controller_.onEncoderB(db); dirty_ = true; }

        if (in.nowMs < popupUntilMs_) return;

        // The two engine pages change on their own; the others are read from the module's
        // memory and follow what MIDI does to it, only less often.
        const bool live = controller_.isDiagPage() || controller_.isLcdPage();
        const uint32_t since = in.nowMs - lastDrawMs_;
        if ((dirty_ && since > 50u) || since > (live ? 250u : 500u)) {
            draw(d, in.nowMs);
            dirty_ = false;
            lastDrawMs_ = in.nowMs;
        } else if (marquee_ && (in.nowMs - lastMarqueeMs_) >= 40u) {
            marquee_ = kit::marqueeTick(d, in.nowMs);
            lastMarqueeMs_ = in.nowMs;
        }
    }

    // -------------------------------------------------------- Persistence
    uint16_t settingsVersion() const override { return CM_SETTINGS_VERSION; }
    size_t settingsSize() const override { return sizeof(CMSettingsV1); }

    void settingsSave(uint8_t* buffer, size_t size) const override {
        if (size < sizeof(CMSettingsV1)) return;
        CMSettingsV1 s{};
        controller_.exportSettings(s);
        memcpy(buffer, &s, sizeof(s));
    }

    void settingsLoad(const uint8_t* buffer, size_t size) override {
        if (size < sizeof(CMSettingsV1) || !ok_) return;    // short record: keep the factory settings
        CMSettingsV1 s{};
        memcpy(&s, buffer, sizeof(s));
        controller_.importSettings(s);
        dirty_ = true;
    }

    // A flash write stalls the CPU for milliseconds; wait until nothing sounds.
    bool settingsSaveAllowed() const override { return !ok_ || !synth_.hasActivePartials(); }

private:
    void draw(picoface::ui::Display& d, uint32_t nowMs) {
        namespace kit = picoface::ui::kit;
        marquee_ = false;

        if (controller_.isLcdPage()) {
            // The CM-32L has no display, but the firmware it shares with the MT-32 keeps
            // one up to date, and games and sequences write to it.
            char lcd[21];
            const bool midiLed = synth_.getDisplayState(lcd);
            const char* rows[2] = {lcd, midiLed ? "MIDI" : ""};
            kit::diagnostics(d, controller_.pageName(), rows, 2);
            d.flush();
            return;
        }

        if (controller_.isDiagPage()) {
            MT32Emu::PartialState states[MT32Emu::DEFAULT_MAX_PARTIALS];
            synth_.getPartialStates(states);
            unsigned active = 0;
            for (MT32Emu::PartialState s : states)
                if (s != MT32Emu::PartialState_INACTIVE) ++active;
            // The peak is the worst block of the last three seconds or so, long enough to read.
            if (worst_.load > shown_.load || nowMs - shownMs_ > 3000u) {
                shown_ = worst_;
                shownMs_ = nowMs;
            }
            worst_ = Worst{};
            char r0[22], r1[22], r2[22], r3[22];
            snprintf(r0, sizeof r0, "CPU %lu%%  peak %lu%%",
                     (unsigned long)(loadAvg_ / 10), (unsigned long)(shown_.load / 10));
            // Where core0's time goes: generating partials, then mix and reverb. Each is
            // the time until both cores are done with it.
            snprintf(r1, sizeof r1, "gen %lu%%  mix %lu%%",
                     (unsigned long)(genAvg_ / 10), (unsigned long)(mixAvg_ / 10));
            // The peak block taken apart: its share of generation and of mix/reverb, the MIDI
            // events it had to play and the runs it was rendered in. What is left of the peak
            // after gen and mix went into events and short runs.
            snprintf(r2, sizeof r2, "pk g%lu m%lu e%lu r%lu",
                     (unsigned long)(shown_.gen / 10), (unsigned long)(shown_.mix / 10),
                     (unsigned long)shown_.events, (unsigned long)shown_.runs);
            // CPU time to generate one sample of one partial, in nanoseconds, for synth and
            // for PCM partials; then sounding partials and underruns.
            snprintf(r3, sizeof r3, "s%lu p%lu P%u U%lu",
                     (unsigned long)synthNs_, (unsigned long)pcmNs_, active,
                     (unsigned long)(g_i2s_underrun_count > 999 ? 999 : g_i2s_underrun_count));
            const char* rows[4] = {r0, r1, r2, r3};
            kit::diagnostics(d, controller_.pageName(), rows, 4);
            d.flush();
            return;
        }

        marquee_ = cmDrawPage(d, controller_, nowMs);
    }

    // Into the engine's queue; the next render() plays it. Same core as render(), so no
    // further hand-over is needed. A full queue drops the message.
    void play(uint8_t status, uint8_t ch, uint8_t d1, uint8_t d2) {
        if (!ok_) return;
        if (synth_.playMsg(uint32_t(status | (ch & 0x0F)) | (uint32_t(d1 & 0x7F) << 8) |
                           (uint32_t(d2 & 0x7F) << 16)))
            ++eventsQueued_;
    }

    // Encoder A on the diagnostics page: notes held on the selected part, so that the load
    // can be read without a MIDI source.
    void holdTestNotes(int want) {
        if (want < 0) want = 0;
        if (want > kTestNotesMax) want = kTestNotesMax;
        // Where the selected part listens: the system area's channel table, 10 00 0D on.
        const uint8_t ch = module_.read(0x10000D + uint32_t(controller_.part()));
        if (ch > 15) return;
        while (testNotes_ < want) play(0x90, ch, kTestNotes[testNotes_++], 100);
        while (testNotes_ > want) play(0x80, ch, kTestNotes[--testNotes_], 64);
    }

    struct Worst {
        uint32_t load = 0, gen = 0, mix = 0, events = 0, runs = 0;
    };

    MT32Emu::Synth synth_;
    Core1Helper core1_;
    CM_SynthModule module_{synth_, cm_control_rom};
    CM_Controller controller_{module_};
    bool ok_ = false;

    bool dirty_ = true;
    bool marquee_ = false;
    uint32_t lastDrawMs_ = 0;
    uint32_t lastMarqueeMs_ = 0;
    uint32_t popupUntilMs_ = 0;
    int testNotes_ = 0;

    uint32_t loadAvg_ = 0;      // tenths of a percent
    uint32_t genAvg_ = 0;
    uint32_t mixAvg_ = 0;
    Worst worst_;               // worst block since the diagnostics page was last drawn
    Worst shown_;               // the one on the screen
    uint32_t shownMs_ = 0;
    uint32_t eventsQueued_ = 0; // since the last render()
    uint32_t runsSeen_ = 0;
    uint32_t jobSums_[4] = {0, 0, 0, 0};
    uint32_t synthNs_ = 0;
    uint32_t pcmNs_ = 0;
};

}  // namespace

PICOFACE_REGISTER_INSTRUMENT(CMInstrument)
