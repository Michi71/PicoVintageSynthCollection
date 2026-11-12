// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "CM_Controller.h"

#include <cmath>
#include <cstdio>

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The MT-32 family's memory, from its MIDI implementation.
constexpr uint32_t kPatchTemp = 0x030000;   // 16 bytes for each of the nine parts
constexpr uint32_t kSystem    = 0x100000;

// Offsets into a part's patch temp area.
constexpr int kPartKeyShift = 2;            // 0..48, 24 in the middle
constexpr int kPartReverb   = 6;            // 0 off, 1 on
constexpr int kPartLevel    = 8;            // 0..100
constexpr int kPartPan      = 9;            // 0..14: 0 is right, 14 is left

// Offsets into the system area.
constexpr int kSysTune        = 0x00;       // 0..127
constexpr int kSysReverbMode  = 0x01;       // 0..3
constexpr int kSysReverbTime  = 0x02;       // 0..7
constexpr int kSysReverbLevel = 0x03;       // 0..7
constexpr int kSysChannels    = 0x0D;       // nine bytes: parts 1-8, rhythm; 0..15, 16 = off
constexpr int kSysVolume      = 0x16;       // 0..100

// A Roland address has seven bits to the byte, so an offset is added in the
// linear form and the result put back.
uint32_t address(uint32_t base, int offset) {
    uint32_t linear = ((base & 0x7F0000) >> 2) | ((base & 0x7F00) >> 1) | (base & 0x7F);
    linear += uint32_t(offset);
    return ((linear << 2) & 0x7F0000) | ((linear << 1) & 0x7F00) | (linear & 0x7F);
}

const char* const kReverbModes[4] = {"Room", "Hall", "Plate", "Delay"};

}  // namespace

// ---------------------------------------------------------------------------

uint8_t CM_Controller::partByte(int offset) const {
    return module_.read(address(kPatchTemp, part_ * 16 + offset));
}

void CM_Controller::setPartByte(int offset, int value) {
    module_.write(address(kPatchTemp, part_ * 16 + offset), uint8_t(value));
}

uint8_t CM_Controller::systemByte(int offset) const {
    return module_.read(address(kSystem, offset));
}

void CM_Controller::setSystemByte(int offset, int value) {
    module_.write(address(kSystem, offset), uint8_t(value));
}

int CM_Controller::channelLayout() const {
    if (systemByte(kSysChannels + kRhythm) != 9) return -1;
    const int first = systemByte(kSysChannels);
    if (first != 0 && first != 1) return -1;
    for (int p = 1; p < kRhythm; ++p)
        if (systemByte(kSysChannels + p) != first + p) return -1;
    return first == 1 ? 0 : 1;
}

void CM_Controller::begin() {
    for (int p = 0; p < kRhythm; ++p) program_[p] = module_.defaultProgram(p);
    panel_.masterVolume = systemByte(kSysVolume);
    panel_.reverbMode = systemByte(kSysReverbMode);
    panel_.reverbTime = systemByte(kSysReverbTime);
    panel_.reverbLevel = systemByte(kSysReverbLevel);
    panel_.masterTune = systemByte(kSysTune);
    panel_.channels = channelLayout() == 1 ? 1 : 0;
}

void CM_Controller::applyPanel() {
    setSystemByte(kSysVolume, panel_.masterVolume);
    setSystemByte(kSysReverbMode, panel_.reverbMode);
    setSystemByte(kSysReverbTime, panel_.reverbTime);
    setSystemByte(kSysReverbLevel, panel_.reverbLevel);
    setSystemByte(kSysTune, panel_.masterTune);
    for (int p = 0; p < kRhythm; ++p)
        setSystemByte(kSysChannels + p, panel_.channels ? p : p + 1);
    setSystemByte(kSysChannels + kRhythm, 9);
}

void CM_Controller::importSettings(const CMSettingsV1& s) {
    panel_.masterVolume = uint8_t(clampi(s.masterVolume, 0, 100));
    panel_.reverbMode = uint8_t(clampi(s.reverbMode, 0, 3));
    panel_.reverbTime = uint8_t(clampi(s.reverbTime, 0, 7));
    panel_.reverbLevel = uint8_t(clampi(s.reverbLevel, 0, 7));
    panel_.masterTune = uint8_t(clampi(s.masterTune, 0, 127));
    panel_.channels = s.channels ? 1 : 0;
    applyPanel();
}

void CM_Controller::reset() {
    module_.reset();
    midiReset();
    applyPanel();
}

void CM_Controller::midiReset() {
    for (int p = 0; p < kRhythm; ++p) program_[p] = module_.defaultProgram(p);
}

void CM_Controller::midiProgramChange(uint8_t channel, uint8_t program) {
    // A channel can be assigned to several parts; the module plays them all.
    for (int p = 0; p < kRhythm; ++p)
        if (systemByte(kSysChannels + p) == channel) program_[p] = program & 0x7F;
}

// ---------------------------------------------------------------------------

void CM_Controller::onEncoderSel(int8_t delta) {
    if (!delta) return;
    page_ = (page_ + delta) % kPageCount;
    if (page_ < 0) page_ += kPageCount;
}

void CM_Controller::onEncoderA(int8_t delta) {
    if (!delta) return;
    switch (page_) {
        case kPagePart:
            part_ = clampi(part_ + delta, 0, kParts - 1);
            break;
        case kPageMix:
            setPartByte(kPartLevel, clampi(partByte(kPartLevel) + delta, 0, 100));
            break;
        case kPageSetup:
            if (part_ != kRhythm) setPartByte(kPartReverb, delta > 0 ? 1 : 0);
            break;
        case kPageMaster:
            panel_.masterVolume = uint8_t(clampi(systemByte(kSysVolume) + delta, 0, 100));
            setSystemByte(kSysVolume, panel_.masterVolume);
            break;
        case kPageReverb:
            panel_.reverbMode = uint8_t(clampi(systemByte(kSysReverbMode) + delta, 0, 3));
            setSystemByte(kSysReverbMode, panel_.reverbMode);
            break;
        case kPageMidi:
            panel_.channels = delta > 0 ? 1 : 0;
            for (int p = 0; p < kRhythm; ++p)
                setSystemByte(kSysChannels + p, panel_.channels ? p : p + 1);
            setSystemByte(kSysChannels + kRhythm, 9);
            break;
        default: break;
    }
}

void CM_Controller::onEncoderB(int8_t delta) {
    if (!delta) return;
    switch (page_) {
        case kPagePart:
            if (part_ != kRhythm) {
                program_[part_] = clampi(program_[part_] + delta, 0, 127);
                const uint8_t channel = systemByte(kSysChannels + part_);
                if (channel < 16) module_.programChange(channel, uint8_t(program_[part_]));
            }
            break;
        case kPageMix:
            // To the right on the encoder is to the right in the stereo image,
            // which is down in the module's numbers.
            if (part_ != kRhythm) setPartByte(kPartPan, clampi(partByte(kPartPan) - delta, 0, 14));
            break;
        case kPageSetup:
            if (part_ != kRhythm) setPartByte(kPartKeyShift, clampi(partByte(kPartKeyShift) + delta, 0, 48));
            break;
        case kPageMaster:
            panel_.reverbLevel = uint8_t(clampi(systemByte(kSysReverbLevel) + delta, 0, 7));
            setSystemByte(kSysReverbLevel, panel_.reverbLevel);
            break;
        case kPageReverb:
            panel_.reverbTime = uint8_t(clampi(systemByte(kSysReverbTime) + delta, 0, 7));
            setSystemByte(kSysReverbTime, panel_.reverbTime);
            break;
        case kPageMidi:
            panel_.masterTune = uint8_t(clampi(systemByte(kSysTune) + delta, 0, 127));
            setSystemByte(kSysTune, panel_.masterTune);
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------

const char* CM_Controller::pageName() const {
    const char* base;
    switch (page_) {
        case kPagePart:   return "Part";
        case kPageMix:    base = "Mix"; break;
        case kPageSetup:  base = "Setup"; break;
        case kPageMaster: return "Master";
        case kPageReverb: return "Reverb";
        case kPageMidi:   return "MIDI";
        case kPageLcd:    return "Display";
        default:          return "Diag";
    }
    // The two pages that edit a part say which.
    if (part_ == kRhythm) snprintf(pageName_, sizeof pageName_, "%s R", base);
    else snprintf(pageName_, sizeof pageName_, "%s %d", base, part_ + 1);
    return pageName_;
}

void CM_Controller::partNumber(char* out, size_t n) const {
    if (part_ == kRhythm) snprintf(out, n, "R");
    else snprintf(out, n, "%d", part_ + 1);
}

void CM_Controller::partName(char* out, size_t n) const {
    if (part_ == kRhythm) snprintf(out, n, "Rhythm");
    else module_.timbreName(part_, out, n);
}

void CM_Controller::partSub(char* out, size_t n) const {
    // The program is the page's second value and stands below; here is where the part listens.
    const int channel = systemByte(kSysChannels + part_);
    if (channel < 16) snprintf(out, n, "MIDI channel %d", channel + 1);
    else snprintf(out, n, "MIDI channel off");
}

CM_Controller::Value CM_Controller::valueA() const {
    Value v{"", "", -1.0f};
    switch (page_) {
        case kPagePart:
            v.name = "Part";
            partNumber(v.text, sizeof v.text);
            v.norm = float(part_) / float(kParts - 1);
            break;
        case kPageMix: {
            const int level = partByte(kPartLevel);
            v.name = "Level";
            snprintf(v.text, sizeof v.text, "%d", level);
            v.norm = level / 100.0f;
            break;
        }
        case kPageSetup:
            v.name = "Reverb";
            if (part_ == kRhythm) snprintf(v.text, sizeof v.text, "-");     // set per key on this part
            else snprintf(v.text, sizeof v.text, "%s", partByte(kPartReverb) ? "On" : "Off");
            break;
        case kPageMaster: {
            const int volume = systemByte(kSysVolume);
            v.name = "Volume";
            snprintf(v.text, sizeof v.text, "%d", volume);
            v.norm = volume / 100.0f;
            break;
        }
        case kPageReverb: {
            const int mode = systemByte(kSysReverbMode);
            v.name = "Mode";
            snprintf(v.text, sizeof v.text, "%s", kReverbModes[mode & 3]);
            break;
        }
        case kPageMidi: {
            const int layout = channelLayout();
            v.name = "Channels";
            snprintf(v.text, sizeof v.text, "%s", layout == 0 ? "2-10" : layout == 1 ? "1-8,10" : "Custom");
            break;
        }
        default: break;
    }
    return v;
}

CM_Controller::Value CM_Controller::valueB() const {
    Value v{"", "", -1.0f};
    switch (page_) {
        case kPagePart:
            v.name = "Prog";
            if (part_ == kRhythm) {
                snprintf(v.text, sizeof v.text, "-");
            } else {
                snprintf(v.text, sizeof v.text, "%d", program_[part_] + 1);
                v.norm = program_[part_] / 127.0f;
            }
            break;
        case kPageMix: {
            v.name = "Pan";
            if (part_ == kRhythm) {                                         // set per key on this part
                snprintf(v.text, sizeof v.text, "-");
                break;
            }
            const int pan = clampi(partByte(kPartPan), 0, 14);
            if (pan == 7) snprintf(v.text, sizeof v.text, "C");
            else if (pan < 7) snprintf(v.text, sizeof v.text, "R%d", 7 - pan);
            else snprintf(v.text, sizeof v.text, "L%d", pan - 7);
            v.norm = (14 - pan) / 14.0f;
            break;
        }
        case kPageSetup:
            v.name = "Key";
            if (part_ == kRhythm) snprintf(v.text, sizeof v.text, "-");
            else {
                const int shift = clampi(partByte(kPartKeyShift), 0, 48);
                snprintf(v.text, sizeof v.text, "%+d", shift - 24);
                v.norm = shift / 48.0f;
            }
            break;
        case kPageMaster: {
            const int level = systemByte(kSysReverbLevel);
            v.name = "Reverb";
            snprintf(v.text, sizeof v.text, "%d", level);
            v.norm = level / 7.0f;
            break;
        }
        case kPageReverb: {
            const int time = systemByte(kSysReverbTime);
            v.name = "Time";
            snprintf(v.text, sizeof v.text, "%d", time + 1);
            v.norm = time / 7.0f;
            break;
        }
        case kPageMidi: {
            // In Hz, the way the LAPC-I's manual gives the range: 427.5 to 452.6, a semitone
            // across the 128 steps with 440 in the middle. The module starts at 442.
            const int tune = systemByte(kSysTune);
            v.name = "Tune";
            snprintf(v.text, sizeof v.text, "%.1f", double(440.0f * std::exp2((tune - 64) / 1536.0f)));
            v.norm = tune / 127.0f;
            break;
        }
        default: break;
    }
    return v;
}
