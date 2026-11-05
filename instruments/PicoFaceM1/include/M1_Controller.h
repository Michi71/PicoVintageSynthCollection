// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// M1_Controller.h -- the panel and the M1's modes: Program / Combination
// play, the edit buffers, Edit Program / Edit Combi, Global and the drum kits,
// Write and Compare, and the memory the SysEx layer loads into.
//
// Controls: SEL turns the pages, A and B the two values shown. A long press
// on SEL opens the menu. In an edit page a short press on SEL opens the list
// of parameter groups (the M1's own edit pages); a long press leaves.
#ifndef M1_CONTROLLER_H
#define M1_CONTROLLER_H

#include <cstddef>
#include <cstdint>

#include "M1_Bridge.h"
#include "M1_SysEx.h"
#include "m1_params.h"
#include "m1_settings.h"
#include "picoface/list_view.h"
#include "picoface/ui.h"

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

class M1_Controller : public M1_SysExHost {
public:
    explicit M1_Controller(M1_Bridge& bridge);

    void setSysEx(M1_SysEx* s) { sysex_ = s; }

    // One UI tick; returns true if the screen needs a redraw.
    bool input(const picoface::ui::InputState& in);
    // Draws the whole screen. Returns true if a name is scrolling.
    bool draw(picoface::ui::Display& d, uint32_t nowMs);

    uint8_t midiChannel() const { return midiCh_; }   // 0..15, 16 = Omni
    int     keyTranspose() const;                      // global, semitones

    // MIDI program change: a program in Program mode, a combination in
    // Combination mode, as on the M1.
    void programChange(uint8_t pc);

    void exportSettings(M1SettingsV1& s) const;
    void importSettings(const M1SettingsV1& s);

    bool memoryDirtySince(uint32_t& ms) const { ms = memChangedMs_; return bridge_.memory().dirty(); }

    // ---- M1_SysExHost
    int  m1Mode() const override;
    void setM1Mode(int mode) override;
    uint8_t* programEdit() override { return progEdit_; }
    uint8_t* combiEdit() override { return combiEdit_; }
    void programEditLoaded() override;
    void combiEditLoaded() override;
    bool writeProgram(int n) override;
    bool writeCombi(int n) override;
    void memoryLoaded() override;
    bool parameterChange(int page, int pos, int value) override;
    int  globalChannel() const override { return midiCh_; }
    m1::Memory& memory() override { return bridge_.memory(); }

private:
    enum class State : uint8_t { Play, Menu, Edit, Groups, Write, Send, Confirm };
    enum class Target : uint8_t { Program, Combi, Global, Kit };

    void selectProgram(int n);
    void selectCombi(int n);
    void applyGlobal();
    void touchMemory(size_t offset, size_t len);

    // edit pages
    void buildEdit(Target t);
    uint8_t* editBlock();
    void editStep(int which, int delta);
    void edited(const m1::ParamDef& p);
    void openMenu();
    void menuChoose(int i);

    // drawing
    bool drawPlay(picoface::ui::Display& d, uint32_t nowMs);
    void drawEdit(picoface::ui::Display& d);
    void drawWrite(picoface::ui::Display& d);

    M1_Bridge& bridge_;
    M1_SysEx* sysex_ = nullptr;

    State   state_ = State::Play;
    M1Page  page_ = M1Page::PATCH;
    uint8_t mode_ = 0;         // 0 Program, 1 Combination (the persisted panel mode)
    uint8_t program_ = 0, combi_ = 0;
    uint8_t volume_ = 80;
    uint8_t voices_ = 16;
    uint8_t midiCh_ = 16;
    int8_t  tune_ = 0;
    uint8_t fxOn_ = 1;

    uint8_t progEdit_[143] = {0};
    uint8_t combiEdit_[124] = {0};
    bool    edited_ = false;
    bool    comparing_ = false;
    uint32_t memChangedMs_ = 0;
    uint32_t nowMs_ = 0;

    // flat list of the parameters the current edit target shows
    static constexpr int kMaxParams = 160;
    static constexpr int kMaxGroups = 40;
    Target  target_ = Target::Program;
    m1::ParamDef params_[kMaxParams];
    uint8_t paramGroup_[kMaxParams] = {0};
    int     nParams_ = 0;
    const char* groupNames_[kMaxGroups] = {nullptr};
    char    groupBuf_[kMaxGroups][12] = {{0}};
    uint8_t groupFirst_[kMaxGroups] = {0};
    int     nGroups_ = 0;
    int     cursor_ = 0;           // index of the first of the two shown params
    uint8_t kit_ = 0, kitInst_ = 0;

    // menus
    picoface::ui::ListView list_;
    const char* menu_[12] = {nullptr};
    uint8_t menuAction_[12] = {0};
    int     nMenu_ = 0;
    int     writeTo_ = 0;
    uint8_t confirmAction_ = 0;
    const char* popup_ = nullptr;
    uint32_t popupUntil_ = 0;
    bool    selWasDown_ = false, selLongSeen_ = false;   // SEL click on release
};

#endif // M1_CONTROLLER_H
