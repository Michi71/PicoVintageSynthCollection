// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// cm_ui_shots.cpp - the PicoFaceCM front panel, rendered to PBM.
//
// The real CM_Controller, driven with the encoder calls the panel makes, and
// drawn with the firmware's own cmDrawPage(). What stands in is the module:
// the engine needs a ROM set that is not in this repository, so the panel
// talks to a few bytes of memory laid out like the CM-32L's, with the
// settings the module has after power-up. Timbre names are placeholders.
//
//   ../ui/build_ui_kit.sh    (once, builds the u8g2 archive)
//   ./build_cm_ui.sh && ./cm_ui_shots out && ../ui/to_png.sh out

#include "ui_shot.h"

#include "CM_Controller.h"
#include "CM_Ui.h"

#include <cstdio>
#include <cstring>

namespace {

class FakeModule final : public CM_Module {
public:
    FakeModule() { reset(); }

    uint8_t read(uint32_t address) override {
        const uint8_t* p = at(address);
        return p ? *p : 0;
    }
    void write(uint32_t address, uint8_t value) override {
        if (uint8_t* p = at(address)) *p = value;
    }
    void programChange(uint8_t channel, uint8_t program) override {
        for (int part = 0; part < 8; ++part)
            if (system_[0x0D + part] == channel) program_[part] = program;
    }
    void timbreName(int part, char* out, size_t n) override {
        std::snprintf(out, n, "Timbre %d", program_[part] + 1);
    }
    uint8_t defaultProgram(int part) override {
        static const uint8_t programs[8] = {68, 48, 95, 78, 41, 3, 110, 122};
        return programs[part];
    }
    void reset() override {
        static const uint8_t pans[9] = {7, 8, 7, 8, 4, 10, 0, 14, 7};
        std::memset(patchTemp_, 0, sizeof patchTemp_);
        for (int part = 0; part < 9; ++part) {
            patchTemp_[part][2] = 24;       // key shift
            patchTemp_[part][6] = 1;        // reverb on
            patchTemp_[part][8] = 80;       // level
            patchTemp_[part][9] = pans[part];
            if (part < 8) program_[part] = defaultProgram(part);
        }
        std::memset(system_, 0, sizeof system_);
        system_[0x00] = 74;                 // 442 Hz
        system_[0x02] = 5;                  // reverb time
        system_[0x03] = 3;                  // reverb level
        for (int part = 0; part < 9; ++part) system_[0x0D + part] = uint8_t(part + 1);
        system_[0x16] = 100;                // master volume
    }

private:
    uint8_t* at(uint32_t address) {
        const uint32_t linear = ((address & 0x7F0000) >> 2) | ((address & 0x7F00) >> 1) | (address & 0x7F);
        const uint32_t patchTemp = 0x030000 >> 2, system = 0x100000 >> 2;
        if (linear >= patchTemp && linear < patchTemp + sizeof patchTemp_)
            return &patchTemp_[0][0] + (linear - patchTemp);
        if (linear >= system && linear < system + sizeof system_) return system_ + (linear - system);
        return nullptr;
    }
    uint8_t patchTemp_[9][16];
    uint8_t system_[0x17];
    uint8_t program_[8];
};

const char* g_dir = ".";

void shot(CM_Controller& c, const char* name) {
    uishot::begin();
    cmDrawPage(uishot::display(), c, 0);
    uishot::save(g_dir, name);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    FakeModule module;
    CM_Controller c(module);
    c.begin();

    shot(c, "cm_part1");                            // as it powers up
    c.onEncoderA(1); c.onEncoderB(-48);
    shot(c, "cm_part2_program");                    // part 2, turned to program 1
    c.onEncoderSel(1); c.onEncoderA(15); c.onEncoderB(4);
    shot(c, "cm_mix");                              // level and pan of that part
    c.onEncoderSel(1); c.onEncoderA(-1); c.onEncoderB(12);
    shot(c, "cm_setup");                            // reverb off, an octave up
    c.onEncoderSel(1); c.onEncoderA(-25);
    shot(c, "cm_master");
    c.onEncoderSel(1); c.onEncoderA(2);
    shot(c, "cm_reverb");
    c.onEncoderSel(1);
    shot(c, "cm_midi");                             // channels as on the original
    c.onEncoderA(1); c.onEncoderB(-10);
    shot(c, "cm_midi_changed");                     // parts on 1-8, tuned to 440
    c.onEncoderSel(-5); c.onEncoderA(8);
    shot(c, "cm_part_rhythm");
    c.onEncoderSel(1);
    shot(c, "cm_mix_rhythm");                       // no pan on the rhythm part

    // The settings survive a round trip, and a reset keeps them.
    CMSettingsV1 s{};
    c.exportSettings(s);
    FakeModule module2;
    CM_Controller c2(module2);
    c2.begin();
    c2.importSettings(s);
    CMSettingsV1 t{};
    c2.exportSettings(t);
    c2.reset();
    CMSettingsV1 u{};
    c2.exportSettings(u);
    const bool same = std::memcmp(&s, &t, sizeof s) == 0 && std::memcmp(&s, &u, sizeof s) == 0 &&
                      module2.read(0x100016) == s.masterVolume && module2.read(0x10000D) == 0;
    std::printf("settings round trip: %s\n", same ? "pass" : "FAIL");
    return same ? 0 : 1;
}
