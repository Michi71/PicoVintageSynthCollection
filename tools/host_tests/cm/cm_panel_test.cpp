// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// cm_panel_test.cpp - the PicoFaceCM panel against the real engine.
//
// CM_Controller and CM_SynthModule as the firmware has them, on the
// instrument's own mt32emu, with a CM-32L ROM set. Checks that what the panel
// writes arrives where it should: each value is turned, the module's memory is
// read back, and where a value can be heard - level, pan, the reverb switch,
// the channel layout - the rendered audio is measured.
//
// Needs the two ROM images in instruments/PicoFaceCM/roms; build_cm_panel.sh
// skips the run without them.
//
//   ./build_cm_panel.sh && ./cm_panel_test <control rom> <pcm rom>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "File.h"
#include "ROMInfo.h"
#include "Synth.h"

#include "CM_Controller.h"
#include "CM_SynthModule.h"

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failed;
}

std::vector<uint8_t> readFile(const char* path) {
    std::vector<uint8_t> out;
    if (FILE* f = std::fopen(path, "rb")) {
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
        std::fclose(f);
    }
    return out;
}

struct Level {
    double left, right;
};

// Renders `blocks` blocks of 64 frames and returns the RMS of the two channels.
Level render(MT32Emu::Synth& synth, int blocks) {
    int16_t buf[2 * 64];
    double l = 0.0, r = 0.0;
    for (int i = 0; i < blocks; ++i) {
        synth.render(buf, 64);
        for (int k = 0; k < 64; ++k) {
            l += double(buf[2 * k]) * buf[2 * k];
            r += double(buf[2 * k + 1]) * buf[2 * k + 1];
        }
    }
    const double n = blocks * 64.0;
    return {std::sqrt(l / n), std::sqrt(r / n)};
}

// One note on a MIDI channel: its level while it sounds, then it is released and left to die.
Level note(MT32Emu::Synth& synth, int channel, int key = 60) {
    synth.playMsg(uint32_t(0x90 | channel) | (uint32_t(key) << 8) | (100u << 16));
    const Level lv = render(synth, 150);        // 300 ms
    synth.playMsg(uint32_t(0x80 | channel) | (uint32_t(key) << 8) | (64u << 16));
    synth.playMsg(uint32_t(0xB0 | channel) | (123u << 8));
    for (int i = 0; i < 4000 && synth.isActive(); ++i) render(synth, 1);
    return lv;
}

std::string text(const CM_Controller::Value& v) { return v.text; }

void page(CM_Controller& c, int index) {
    while (c.pageIndex() != index) c.onEncoderSel(1);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: cm_panel_test CONTROL.ROM PCM.ROM\n");
        return 2;
    }
    const std::vector<uint8_t> controlRom = readFile(argv[1]), pcmRom = readFile(argv[2]);
    std::vector<int16_t> pcmWords(pcmRom.size() / 2);
    for (size_t i = 0; i < pcmWords.size(); ++i)
        pcmWords[i] = MT32Emu::Synth::unscramblePCMWord(pcmRom[2 * i], pcmRom[2 * i + 1]);

    // The synth as CM_Instrument::init() sets it up, without the second core.
    MT32Emu::ArrayFile controlFile(controlRom.data(), controlRom.size());
    const MT32Emu::ROMImage* control = MT32Emu::ROMImage::makeROMImage(&controlFile);
    MT32Emu::Synth synth;
    synth.setExternalPCMData(pcmWords.data(), pcmWords.size());
    synth.selectRendererType(MT32Emu::RendererType_BIT16S);
    synth.setPerPartialPitchJitter(true);
    synth.setChunkedAbortRuns(true);
    synth.setMIDIDelayMode(MT32Emu::MIDIDelayMode_IMMEDIATE);
    synth.configureMIDIEventQueueSysexStorage(4096);
    if (!control->getROMInfo() || !synth.open(*control, *control, 32, MT32Emu::AnalogOutputMode_DIGITAL_ONLY)) {
        std::fprintf(stderr, "the synth did not open - not the CM-32L v1.02 ROM set?\n");
        return 2;
    }

    CM_SynthModule module(synth, controlRom.data());
    CM_Controller c(module);
    c.begin();
    enum { kPart, kMix, kSetup, kMaster, kReverb, kMidi };

    std::printf("power-up\n");
    char name[16], before[16], sub[24];
    c.partName(before, sizeof before);
    c.partSub(sub, sizeof sub);
    check(std::strlen(before) > 0, "part 1 has a timbre name");
    check(text(c.valueB()) == std::to_string(module.defaultProgram(0) + 1), "its program is the power-up one");
    check(std::string(sub) == "MIDI channel 2", "part 1 listens on channel 2");
    page(c, kMaster);
    check(text(c.valueA()) == "100", "master volume 100");
    page(c, kMidi);
    check(text(c.valueA()) == "2-10", "channels as on the original");
    // "Silent" below is one percent of the sounding note: the reverb never comes to rest at
    // exactly zero, and the module's level 0 is very quiet rather than off.
    const double sounding = note(synth, 1).left;
    check(sounding > 20.0, "a note on channel 2 sounds");
    check(note(synth, 0).left < sounding / 100.0, "a note on channel 1 does not");

    std::printf("part page\n");
    page(c, kPart);
    c.onEncoderB(-127);                                     // program 1
    render(synth, 1);
    c.partName(name, sizeof name);
    check(std::strcmp(name, before) != 0, "program 1 gives part 1 another timbre");
    check(text(c.valueB()) == "1", "and the page says program 1");

    std::printf("setup and mix pages\n");
    page(c, kSetup);
    c.onEncoderA(-1);                                       // reverb off: nothing leaks to the other side
    c.onEncoderB(12);
    render(synth, 1);
    check(module.read(0x030006) == 0 && text(c.valueA()) == "Off", "reverb switch off");
    check(module.read(0x030002) == 36 && text(c.valueB()) == "+12", "key shift +12");
    c.onEncoderB(-12);
    page(c, kMix);
    c.onEncoderB(14);                                       // all the way to the right
    render(synth, 1);
    Level lv = note(synth, 1);
    check(text(c.valueB()) == "R7" && module.read(0x030009) == 0, "pan R7 is 0 in the module");
    check(lv.right > 20.0 && lv.left < lv.right / 50.0, "and sounds on the right");
    c.onEncoderB(-14);
    render(synth, 1);
    lv = note(synth, 1);
    check(text(c.valueB()) == "L7" && module.read(0x030009) == 14, "pan L7 is 14");
    check(lv.left > 20.0 && lv.right < lv.left / 50.0, "and sounds on the left");
    const double loud = lv.left;
    c.onEncoderA(-40);
    render(synth, 1);
    lv = note(synth, 1);
    check(module.read(0x030008) == 40 && lv.left < loud * 0.8 && lv.left > 1.0, "level 40 is quieter");
    c.onEncoderA(-100);
    render(synth, 1);
    check(note(synth, 1).left < loud / 100.0, "level 0 is as good as silent");

    std::printf("master and reverb pages\n");
    page(c, kMaster);
    c.onEncoderA(-10);
    c.onEncoderB(4);
    page(c, kReverb);
    c.onEncoderA(2);
    c.onEncoderB(-3);
    render(synth, 1);
    char lcd[21];
    synth.getDisplayState(lcd);
    check(module.read(0x100016) == 90 && std::strstr(lcd, "vol: 90") != nullptr, "master volume 90, on the module's display too");
    check(module.read(0x100003) == 7 && module.read(0x100001) == 2 && module.read(0x100002) == 2,
          "reverb level 7, mode Plate, time 3");
    check(text(c.valueA()) == "Plate" && text(c.valueB()) == "3", "and the page says so");

    std::printf("MIDI page\n");
    page(c, kMidi);
    c.onEncoderA(1);
    c.onEncoderB(-10);
    render(synth, 1);
    check(text(c.valueA()) == "1-8,10" && module.read(0x10000D) == 0 && module.read(0x100015) == 9,
          "parts on channels 1-8, rhythm on 10");
    check(text(c.valueB()) == "440.0" && module.read(0x100000) == 64, "tuned to 440.0");
    page(c, kPart);
    c.onEncoderA(1);                                        // part 2, untouched so far
    check(note(synth, 1).left > 20.0, "channel 2 now plays part 2");
    c.partSub(sub, sizeof sub);
    check(std::string(sub) == "MIDI channel 2", "which the part page shows");

    std::printf("what MIDI does shows on the panel\n");
    synth.playMsg(0xC1u | (10u << 8));                      // program 11 on channel 2
    c.midiProgramChange(1, 10);
    static const uint8_t volume50[] = {0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x16, 50, 0x28, 0xF7};
    synth.playSysex(volume50, sizeof volume50);
    render(synth, 1);
    check(text(c.valueB()) == "11", "a program change on channel 2 reaches part 2's page");
    page(c, kMaster);
    check(text(c.valueA()) == "50", "a master volume set by SysEx is what the page shows");

    std::printf("settings and reset\n");
    CMSettingsV1 s{};
    c.exportSettings(s);
    check(s.masterVolume == 90 && s.reverbLevel == 7 && s.reverbMode == 2 && s.reverbTime == 2 &&
          s.masterTune == 64 && s.channels == 1, "the settings are the panel's, not what MIDI set since");
    c.reset();
    render(synth, 1);
    check(module.read(0x100016) == 90 && module.read(0x10000D) == 0 && module.read(0x100001) == 2,
          "a reset keeps the panel's settings");
    page(c, kPart);
    c.onEncoderA(-8);
    c.partName(name, sizeof name);
    check(std::strcmp(name, before) == 0 && module.read(0x030008) > 0, "and gives part 1 its timbre and level back");

    std::printf("%d failed\n", g_failed);
    return g_failed ? 1 : 0;
}
