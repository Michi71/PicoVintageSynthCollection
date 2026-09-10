// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// D5_Controller.h -- what the three encoders do. The select encoder walks the
// pages, the other two edit the two values that page offers.

#ifndef D5_CONTROLLER_H
#define D5_CONTROLLER_H

#include <cstddef>
#include <cstdint>

#include "D5_Bridge.h"
#include "d5_settings.h"

class D5_Controller {
public:
    explicit D5_Controller(D5_Bridge& bridge) : bridge_(bridge) {}

    void onEncoderSel(int8_t delta);
    void onEncoderA(int8_t delta);
    void onEncoderB(int8_t delta);

    const char* title() const;
    const char* pageName() const;

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

    // The patch page shows a name, not a number, and the kit has its own body
    // for that: the number stays put on the left, the name takes the rest of
    // the width and scrolls when it is longer than that.
    bool        isPatchPage() const { return page_ == kPagePatch; }
    void        patchNumber(char* out, size_t n) const;   // "2-37", or "12" with one bank
    const char* patchName() const;
    bool        isDiagPage()  const { return page_ == kPageDiag; }
    const char* patchStructure() const;  // second line under the patch name

    int  pageIndex() const { return page_; }
    int  pageTotal() const { return kPageCount; }

    void exportSettings(D5SettingsV2& s) const;
    void importSettings(const D5SettingsV2& s);

    int midiChannel() const { return midiCh_; }

private:
    // The D-50 keeps chorus and reverb in patch data, not on the panel --
    // there is no global "reverb amount" knob on the original. These two
    // pages edit the patch's own Balance parameters, in its own 0..100.
    enum Page {
        kPagePatch = 0,   // patch          | voice limit
        kPageMix,         // master volume  | tone balance   (pb33)
        kPageReverb,      // reverb balance | reverb type    (pb31/pb30)
        kPageChorus,      // chorus balance | chorus type    (c45/c42)
        kPageChorusMod,   // chorus rate    | chorus depth   (c43/c44)
        kPageEqLow,       // low freq       | low gain       (c37/c38)
        kPageEqHigh,      // high freq      | high gain      (c39/c41)
        kPageEqQ,         // high Q         | -              (c40)
        kPageTune,        // master tune    | MIDI channel
        kPageDiag,        // CPU, underruns, voices - drawn through kit::diagnostics
        kPageCount
    };

    D5_Bridge& bridge_;
    int page_ = kPagePatch;
    int volume_ = 80;
    int voices_ = 8;
    int midiCh_ = 16;      // 16 = Omni
    int tune_ = 0;
};

#endif // D5_CONTROLLER_H
