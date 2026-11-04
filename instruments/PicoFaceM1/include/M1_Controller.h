// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// M1_Controller.h -- the panel: pages, encoders, persisted state.
#ifndef M1_CONTROLLER_H
#define M1_CONTROLLER_H

#include <cstddef>
#include <cstdint>

#include "M1_Bridge.h"
#include "m1_settings.h"

enum class M1Page : uint8_t {
    PATCH = 0,   // A: program / combination number, B: Prog <-> Combi
    VOLUME,
    VOICES,
    TUNE,
    FX,          // effect section on / bypassed
    SYS,         // MIDI channel
    DIAG,
    COUNT
};

class M1_Controller {
public:
    explicit M1_Controller(M1_Bridge& bridge) : bridge_(bridge) {}

    void onEncoderSel(int delta);
    void onEncoderA(int delta);
    void onEncoderB(int delta);

    const char* pageName() const;
    struct Value {
        const char* name;
        char        text[24];
        float       norm;
    };
    Value valueA() const;
    Value valueB() const;

    bool isPatchPage() const { return page_ == M1Page::PATCH; }
    bool isDiagPage()  const { return page_ == M1Page::DIAG; }
    int  pageIndex() const { return (int)page_; }
    int  pageTotal() const { return (int)M1Page::COUNT; }

    uint8_t midiChannel() const { return midiCh_; }   // 0..15, 16 = Omni

    // Program change from MIDI: in Program mode it picks a program, in
    // Combination mode a combination, as on the M1.
    void programChange(uint8_t pc);

    void exportSettings(M1SettingsV1& s) const;
    void importSettings(const M1SettingsV1& s);

private:
    void apply();

    M1_Bridge& bridge_;
    M1Page  page_ = M1Page::PATCH;
    uint8_t mode_ = 0;         // 0 Program, 1 Combination
    uint8_t program_ = 0, combi_ = 0;
    uint8_t volume_ = 80;
    uint8_t voices_ = 16;
    uint8_t midiCh_ = 16;
    int8_t  tune_ = 0;
    uint8_t fxOn_ = 1;
};

#endif // M1_CONTROLLER_H
