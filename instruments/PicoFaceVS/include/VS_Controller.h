// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// VS_Controller.h -- the panel of PicoFaceVS and everything behind it that is
// not audio: which program plays, the edit buffer, Write and Compare, the
// arpeggiator and chord switches, SysEx dumps in both directions and the
// moment the bank goes to flash.
//
// Controls, as on PicoFaceM1: SEL turns the pages, A and B the two values
// shown. A long press on SEL opens the menu. In an edit page a short press on
// SEL opens the list of parameter groups; a long press leaves.
//
// Nothing here needs the Pico SDK, so the host test drives this class as it
// is and renders its screens.
#ifndef VS_CONTROLLER_H
#define VS_CONTROLLER_H

#include <cstddef>
#include <cstdint>

#include "picoface/list_view.h"
#include "picoface/ui.h"
#include "provs/engine.h"
#include "vs_store.h"

// SysEx output, provided by the adapter (both wires) or by the host test.
// vs_sysex_room() is how many bytes may be written right now.
size_t vs_sysex_room();
void   vs_sysex_write(const uint8_t* data, size_t len);

struct VsSettingsV2 {
    uint8_t bank;
    uint8_t program;
    uint8_t fx;         // 0 = as programmed, 1..4 = forced effect type + 1
    uint8_t volume;     // 0..100
    uint8_t arp;        // 0 off, 1 on, 2 latch
    uint8_t arpClock;   // 0 internal, 1 MIDI clock
    uint8_t clocks;     // clocks per arpeggiator step, 1..24
    uint8_t chord;      // 0 off, 1..8 chord set
};
constexpr uint16_t kVsSettingsVersion = 2;

class VS_Controller {
public:
    enum Bank : uint8_t { kBankVs = 0, kBankMini = 1 };
    enum class Page : uint8_t { Program = 0, Level, Arp, Arp2, Diag, Count };

    VS_Controller(provs::Engine& engine, vs::Store& store) : engine_(engine), store_(store) {}

    // After Engine::init(). 'banks' are the .syx files embedded at build time
    // (may be empty); a bank stored in flash takes precedence over them.
    void init(const uint8_t* banks, size_t size);

    // One UI tick; returns true if the screen needs a redraw.
    bool input(const picoface::ui::InputState& in);
    // Draws the whole screen. Returns true if a name is scrolling.
    bool draw(picoface::ui::Display& d, uint32_t nowMs);
    // Every tick: feeds a running dump and writes the bank to flash once
    // things have been quiet. Returns true if the screen needs a redraw.
    bool poll(uint32_t nowMs);

    // MIDI
    void programChange(uint8_t bankLsb, uint8_t program);
    void setVolume(uint8_t volume);                 // 0..100
    void sysExMessage(const uint8_t* data, size_t length);   // one complete message
    void midiClock() { engine_.arpTick(); }

    void setLoad(float percent);
    bool sending() const { return txBank_ || txProgram_; }

    void exportSettings(VsSettingsV2& s) const;
    void importSettings(const VsSettingsV2& s);

    // For the host test.
    Page page() const { return page_; }
    bool edited() const { return edited_; }
    const uint8_t* editBuffer() const { return edit_; }

private:
    enum class State : uint8_t { Play, Menu, Edit, Groups, Write, Send, Confirm };

    static int count(uint8_t bank);
    void select(uint8_t bank, int program);
    void play();                    // the current program into the engine
    void applyOverrides();
    void applyArp();
    void editStep(int which, int delta);
    void write(int slot);
    void factoryReset();
    void openMenu();
    void openSend();
    void menuChoose(int i);
    void popup(const char* text);

    bool drawPlay(picoface::ui::Display& d, uint32_t nowMs);
    void drawEdit(picoface::ui::Display& d);
    void drawWrite(picoface::ui::Display& d);

    provs::Engine& engine_;
    vs::Store& store_;
    const uint8_t* banks_ = nullptr;
    size_t banksSize_ = 0;

    State   state_ = State::Play;
    Page    page_ = Page::Program;
    uint8_t bank_ = kBankVs;
    uint8_t program_ = 0;
    uint8_t fx_ = 0;
    uint8_t volume_ = 80;
    uint8_t arp_ = 0;
    uint8_t arpClock_ = 0;
    uint8_t clocks_ = 6;
    uint8_t chord_ = 0;

    uint8_t edit_[provs::Engine::kVsFields] = {0};
    bool    edited_ = false;
    bool    comparing_ = false;
    int     cursor_ = 0;           // index of the first of the two shown params
    int     writeTo_ = 0;

    // menus
    picoface::ui::ListView list_;
    const char* menu_[8] = {nullptr};
    uint8_t menuAction_[8] = {0};
    int     nMenu_ = 0;
    const char* popup_ = nullptr;
    uint32_t popupUntil_ = 0;
    uint32_t nowMs_ = 0;
    bool    selWasDown_ = false, selLongSeen_ = false;   // SEL click on release

    // dumps going out
    bool    txProgram_ = false;    // one program (the edit buffer) is waiting
    bool    txBank_ = false;
    int     txNext_ = 0;           // next program of a bank dump

    // flash mirror
    uint32_t memChangedMs_ = 0;

    float   load_ = 0.f, loadPeak_ = 0.f;
};

#endif // VS_CONTROLLER_H
