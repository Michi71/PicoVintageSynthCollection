// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// VS_Controller.h -- the panel of PicoFaceVS and everything behind it that is
// not audio: which program plays, the edit buffer, Write and Compare, the
// joystick, the arpeggiator and chord switches, Edit Waveform mode, SysEx in
// both directions and the moment the memory goes to flash.
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
#include "vs_dump.h"
#include "vs_store.h"

struct VsSettingsV4 {
    uint8_t bank;
    uint8_t program;
    uint8_t fx;           // 0 = as programmed, 1..4 = forced effect type + 1
    uint8_t volume;       // 0..100
    uint8_t arp;          // 0 off, 1 on, 2 latch
    uint8_t arpClock;     // 0 internal, 1 MIDI clock
    uint8_t clocks;       // clocks per arpeggiator step, 1..24
    uint8_t chord;        // 0 off, 1..8 chord set
    uint8_t midiMode;     // 0 = mode 1 (omni), 1 = mode 3 (poly), 2 = mode 4 (mono)
    uint8_t rightChannel; // 0..15, the base channel
    uint8_t leftChannel;  // 0..15, the linked program of a split or double
    uint8_t arpChannel;   // 0 = off, 1..16: the arpeggiator's notes go out here
    int8_t  joyX, joyY;   // joystick, -64..+64; x toward C, y toward D
    uint8_t waveNo[4];    // Edit Waveform: the wave of A-D, 0..126
    uint8_t waveHarm[4];  //   its harmonic, 1..32
    int8_t  waveX, waveY; //   and the mix position
};
constexpr uint16_t kVsSettingsVersion = 4;

class VS_Controller {
public:
    enum Bank : uint8_t { kBankVs = 0, kBankMini = 1 };
    enum class Page : uint8_t { Program = 0, Level, Joystick, Arp, Arp2, Midi, Diag, Count };

    VS_Controller(provs::Engine& engine, vs::Store& store) : engine_(engine), store_(store) {}

    // After Engine::init(). 'banks' are the .syx files embedded at build time
    // (may be empty); what is stored in flash takes precedence over them.
    void init(const uint8_t* banks, size_t size);

    // One UI tick; returns true if the screen needs a redraw.
    bool input(const picoface::ui::InputState& in);
    // Draws the whole screen. Returns true if a name is scrolling.
    bool draw(picoface::ui::Display& d, uint32_t nowMs);
    // Every tick: feeds a running dump, computes the levels of a user wave
    // that has changed and writes the memory to flash once things have been
    // quiet. Returns true if the screen needs a redraw.
    bool poll(uint32_t nowMs);

    // MIDI, as it comes off the wire. The receive mode decides what a
    // channel means (owner's manual ch. 14): in mode 1 every channel plays;
    // in mode 3 the right channel does, and in a split or double with a left
    // channel of its own each channel plays one of the two programs over the
    // whole keyboard; in mode 4 eight consecutive channels from the right one
    // play one voice each (four and four from the left and right channels in
    // a split or double), and the channel below reaches all of them.
    // Returns true if the message was for this instrument.
    bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    bool noteOff(uint8_t channel, uint8_t note);
    void controlChange(uint8_t channel, uint8_t cc, uint8_t value);
    void pitchBend(uint8_t channel, int16_t bend);
    void pressure(uint8_t channel, uint8_t value);
    void programChange(uint8_t channel, uint8_t program);
    void setVolume(uint8_t volume);                 // 0..100
    void sysExMessage(const uint8_t* data, size_t length);   // one complete message
    void midiClock() { engine_.arpTick(); }

    void setLoad(float percent);
    bool sending() const { return dump_.busy(); }

    void exportSettings(VsSettingsV4& s) const;
    void importSettings(const VsSettingsV4& s);

    // For the host test.
    Page page() const { return page_; }
    bool edited() const { return edited_; }
    const uint8_t* editBuffer() const { return edit_; }

private:
    enum class State : uint8_t { Play, Menu, Edit, Groups, Write, Send, Confirm, Wave, Midi, Cart };

    // What a channel addresses: all voices, one layer, or one voice.
    struct Target { unsigned voices; int layer; int voice; bool global; };
    bool target(uint8_t channel, Target& t) const;
    void applyMidi();
    void sendParam(int index);
    void sendChannelMessage(uint8_t status, uint8_t d1, uint8_t d2);
    void openCart();
    void drawMidi(picoface::ui::Display& d);

    static int count(uint8_t bank);
    void select(uint8_t bank, int program);
    void play();                    // what is selected into the engine
    void playWaveEdit();
    void applyOverrides();
    void applyArp();
    void editStep(int which, int delta);
    void write(int slot);
    void storeWave();
    void receiveWaves(const uint8_t* data, size_t length);
    void factoryReset();
    void openMenu();
    void openSend();
    void menuChoose(int i);
    void popup(const char* text);
    uint8_t channel() const { return rightChannel_; }

    bool drawPlay(picoface::ui::Display& d, uint32_t nowMs);
    void drawEdit(picoface::ui::Display& d);
    void drawWrite(picoface::ui::Display& d);
    void drawWave(picoface::ui::Display& d);
    static void drawVector(picoface::ui::Display& d, int x, int y);

    provs::Engine& engine_;
    vs::Store& store_;
    VS_Dump dump_;
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
    uint8_t midiMode_ = 0;
    uint8_t rightChannel_ = 0, leftChannel_ = 0;
    uint8_t arpChannel_ = 0;       // 0 = off
    // As on the VS, these two are off after power-up
    bool    overflow_ = false;
    uint8_t midiParams_ = 0;       // bit 0 receive, bit 1 transmit
    uint8_t midiBank_ = 0;         // CC 32, held until the next program change
    uint8_t paramLsb_ = 0, paramMsb_ = 0, dataLsb_ = 0;   // parameter select and data entry
    bool    transposeArmed_ = false;   // the next key transposes the arpeggio
    int8_t  transpose_ = 0;
    uint8_t midiPage_ = 0;
    char    popupText_[12] = {0};
    int8_t  joyX_ = 0, joyY_ = 0;

    uint8_t edit_[provs::Engine::kVsFields] = {0};
    bool    edited_ = false;
    bool    comparing_ = false;
    int     cursor_ = 0;           // index of the first of the two shown params
    int     writeTo_ = 0;

    // Edit Waveform mode
    uint8_t wavePage_ = 0;         // 0..3 wave A-D, 4 mix, 5 store
    uint8_t waveNo_[4] = {32, 32, 32, 32};
    uint8_t waveHarm_[4] = {1, 2, 3, 4};
    int8_t  waveX_ = 0, waveY_ = 0;
    uint8_t waveTo_ = 0;

    // menus
    picoface::ui::ListView list_;
    const char* menu_[12] = {nullptr};
    uint8_t menuAction_[12] = {0};
    int     nMenu_ = 0;
    const char* popup_ = nullptr;
    uint32_t popupUntil_ = 0;
    uint32_t nowMs_ = 0;
    bool    selWasDown_ = false, selLongSeen_ = false;   // SEL click on release

    // flash mirror
    uint32_t memChangedMs_ = 0;

    float   load_ = 0.f, loadPeak_ = 0.f;
};

#endif // VS_CONTROLLER_H
