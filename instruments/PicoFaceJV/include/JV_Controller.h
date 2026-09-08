// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// JV_Controller.h -- panel logic. Encoder Sel cycles the pages (with wrap),
// encoders A and B edit that page's two parameters. The bridge is the single
// source of truth; the controller keeps only what it needs to draw.

#ifndef JV_CONTROLLER_H
#define JV_CONTROLLER_H

#include <cstddef>   // size_t in the line accessors
#include <cstdint>

#include "JV_Bridge.h"
#include "jv_settings.h"

enum class JvPage : uint8_t {
    PATCH = 0,
    VOLUME,
    VOICES,
    TUNE,
    VELO,
    SYS,
    COUNT
};

class JV_Controller {
public:
    explicit JV_Controller(JV_Bridge& bridge);

    void onEncoderSel(int delta);
    void onEncoderA(int delta);
    void onEncoderB(int delta);

    JvPage      currentPage() const { return page_; }
    const char* pageName() const;
    const char* title() const;

    // One value as the shared UI kit wants it: a name, the value already
    // formatted (the controller owns the units), and where it sits on its
    // range. Replaces the old lineA/lineB, which glued name and value into one
    // string - the kit needs them apart to size and place them.
    struct Value {
        const char* name;   // "" = the value names itself and gets the row
        char        text[24];
        float       norm;   // 0..1, or negative where there is no dial position
    };
    Value valueA() const;
    Value valueB() const;

    // The patch page shows a name, not a number, and needs the whole width.
    bool isPatchPage() const { return page_ == JvPage::PATCH; }

    int  pageIndex() const { return (int)page_; }
    int  pageTotal() const { return (int)JvPage::COUNT; }

    uint8_t midiChannel() const { return midiCh_; }   // 0..15, 16 = Omni

    void exportSettings(JvSettingsV1& s) const;
    void importSettings(const JvSettingsV1& s);       // clamps and applies

private:
    void applyPatch();

    JV_Bridge& bridge_;
    JvPage  page_ = JvPage::PATCH;
    uint8_t bank_ = 1;        // 0 User, 1 A, 2 B
    uint8_t patch_ = 0;       // 0..63
    uint8_t volume_ = 80;     // 0..100
    uint8_t voices_ = 16;     // 1..jv::kMaxVoices
    uint8_t midiCh_ = 16;     // 16 = Omni
    int8_t  tune_ = 0;        // cents
    uint8_t veloScale_ = 100; // 0..100 %, 100 = untouched
};

#endif // JV_CONTROLLER_H
