// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// CM_SynthModule.h -- the panel's view of the module (CM_Module), on the
// engine. Reads go straight to the synth's memory; writes and program changes
// go through its MIDI queue like everything else that plays it, so they take
// effect with the next render() and in the order they were made.
//
// Header-only, so that the firmware and the host test that checks the panel
// against the real engine use the very same code.

#ifndef CM_SYNTH_MODULE_H
#define CM_SYNTH_MODULE_H

#include <cstdio>

#include "CM_Controller.h"
#include "Synth.h"

class CM_SynthModule final : public CM_Module {
public:
    // The control ROM is where the parts' power-up programs are.
    CM_SynthModule(MT32Emu::Synth& synth, const uint8_t* controlRom)
        : synth_(synth), controlRom_(controlRom) {}

    uint8_t read(uint32_t address) override {
        MT32Emu::Bit8u value = 0;
        // The synth addresses its memory linearly, not in Roland's seven-bit bytes.
        synth_.readMemory(((address & 0x7F0000) >> 2) | ((address & 0x7F00) >> 1) | (address & 0x7F),
                          1, &value);
        return value;
    }

    void write(uint32_t address, uint8_t value) override {
        // Roland data set 1 to the MT-32 family: device 0x10, model 0x16.
        const uint8_t a2 = uint8_t((address >> 16) & 0x7F), a1 = uint8_t((address >> 8) & 0x7F),
                      a0 = uint8_t(address & 0x7F), v = uint8_t(value & 0x7F);
        const uint8_t sum = uint8_t((a2 + a1 + a0 + v) & 0x7F);
        const uint8_t msg[11] = {0xF0, 0x41, 0x10, 0x16, 0x12, a2, a1, a0, v,
                                 uint8_t((128 - sum) & 0x7F), 0xF7};
        if (synth_.playSysex(msg, sizeof msg)) ++queued_;
    }

    void programChange(uint8_t channel, uint8_t program) override {
        if (synth_.playMsg(uint32_t(0xC0 | (channel & 0x0F)) | (uint32_t(program & 0x7F) << 8)))
            ++queued_;
    }

    void timbreName(int part, char* out, size_t n) override {
        const char* name = synth_.getPatchName(MT32Emu::Bit8u(part));
        std::snprintf(out, n, "%s", name ? name : "");
    }

    uint8_t defaultProgram(int part) override {
        // Control ROM v1.02 of the CM-32L / LAPC-I, the only one the build accepts.
        return controlRom_[0x4F9C + part];
    }

    void reset() override {
        // Any data set to address 7F 00 00 resets the module.
        static const uint8_t msg[11] = {0xF0, 0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x00, 0x01, 0xF7};
        if (synth_.playSysex(msg, sizeof msg)) ++queued_;
    }

    // Messages queued since the last call, for whoever counts MIDI events.
    uint32_t takeQueued() {
        const uint32_t n = queued_;
        queued_ = 0;
        return n;
    }

private:
    MT32Emu::Synth& synth_;
    const uint8_t* controlRom_;
    uint32_t queued_ = 0;
};

#endif // CM_SYNTH_MODULE_H
