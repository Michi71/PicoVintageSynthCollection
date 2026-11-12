// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// CM_Controller.h -- what the three encoders do. The select encoder walks the
// pages, the other two edit the two values that page offers.
//
// A CM-32L has no panel: one volume knob, everything else arrives by MIDI. So
// this panel invents nothing of its own. Every value on it is a byte in the
// module's memory, read from there each time it is drawn and written there
// the way a sequencer would write it, with a Roland data-set message. What a
// MIDI file changes shows up here, and what is set here is what the file
// finds.

#ifndef CM_CONTROLLER_H
#define CM_CONTROLLER_H

#include <cstddef>
#include <cstdint>

#include "cm_settings.h"

// The module as the panel sees it. Addresses are Roland's: three bytes of
// seven bits, as in the MT-32's MIDI implementation (0x100016 is 10 00 16).
class CM_Module {
public:
    virtual ~CM_Module() = default;
    virtual uint8_t read(uint32_t address) = 0;
    virtual void write(uint32_t address, uint8_t value) = 0;
    virtual void programChange(uint8_t channel, uint8_t program) = 0;
    // Name of the timbre on part 0..7, null-terminated, at most 10 characters.
    virtual void timbreName(int part, char* out, size_t n) = 0;
    // The program part 0..7 has after power-up.
    virtual uint8_t defaultProgram(int part) = 0;
    // Back to the state after power-up.
    virtual void reset() = 0;
};

class CM_Controller {
public:
    static constexpr int kParts = 9;        // eight melodic parts and the rhythm part
    static constexpr int kRhythm = 8;

    explicit CM_Controller(CM_Module& module) : module_(module) {}

    // Once, when the module is open: takes its factory settings as the panel's.
    void begin();

    void onEncoderSel(int8_t delta);
    void onEncoderA(int8_t delta);
    void onEncoderB(int8_t delta);

    // The module back to power-up, then the panel's settings again.
    void reset();

    // What arrived by MIDI and the panel cannot read back from the module.
    void midiProgramChange(uint8_t channel, uint8_t program);
    void midiReset();

    const char* pageName() const;

    // One value as the shared UI kit wants it: a name, the value already
    // formatted, and where it sits on its range (negative: no dial position).
    struct Value {
        const char* name;
        char        text[24];
        float       norm;
    };
    Value valueA() const;
    Value valueB() const;

    // The part page shows a name and has the kit's body for that.
    bool isPartPage() const { return page_ == kPagePart; }
    bool isLcdPage()  const { return page_ == kPageLcd; }
    bool isDiagPage() const { return page_ == kPageDiag; }
    void partNumber(char* out, size_t n) const;     // "3", "R"
    void partName(char* out, size_t n) const;       // timbre name, "Rhythm"
    void partSub(char* out, size_t n) const;        // "MIDI channel 3"

    int part() const { return part_; }
    int pageIndex() const { return page_; }
    int pageTotal() const { return kPageCount; }

    void exportSettings(CMSettingsV1& s) const { s = panel_; }
    void importSettings(const CMSettingsV1& s);

private:
    enum Page {
        kPagePart = 0,  // A: part            B: program on it
        kPageMix,       // A: level           B: pan            (of the part)
        kPageSetup,     // A: reverb on/off   B: key shift      (of the part)
        kPageMaster,    // A: master volume   B: reverb level
        kPageReverb,    // A: reverb mode     B: reverb time
        kPageMidi,      // A: channel layout  B: master tune
        kPageLcd,       // what the module's display would show
        kPageDiag,      // the developer's numbers
        kPageCount
    };

    uint8_t partByte(int offset) const;
    void setPartByte(int offset, int value);
    uint8_t systemByte(int offset) const;
    void setSystemByte(int offset, int value);
    int channelLayout() const;      // 0, 1, or -1 for anything else
    void applyPanel();

    CM_Module& module_;
    int page_ = kPagePart;
    int part_ = 0;
    int program_[kParts - 1] = {0, 0, 0, 0, 0, 0, 0, 0};
    CMSettingsV1 panel_ = {100, 0, 5, 3, 64, 0, {0, 0}};
    mutable char pageName_[12] = "";
};

#endif // CM_CONTROLLER_H
