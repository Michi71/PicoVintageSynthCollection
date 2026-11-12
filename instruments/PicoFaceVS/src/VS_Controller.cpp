// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// VS_Controller.cpp -- see VS_Controller.h.

#include "VS_Controller.h"

#include <cstdio>
#include <cstring>

#include "u8g2.h"                   // the vector diamond is drawn with lines

#include "picoface/ui_kit.h"
#include "provs/gen/rom_tables.h"   // kNameChars
#include "vs_params.h"

namespace kit = picoface::ui::kit;
using picoface::ui::Button;
using picoface::ui::Encoder;
using provs::Engine;

namespace {

enum MenuAction : uint8_t {
    A_EDIT, A_WRITE, A_COMPARE, A_WAVE, A_MIDI, A_CART, A_SEND, A_RESET, A_EXIT,
    A_SEND_PROGRAM, A_SEND_BANK, A_SEND_WAVES, A_BACK,
    A_CART_SAVE_PROGS, A_CART_SAVE_WAVES, A_CART_LOAD_PROGS, A_CART_LOAD_WAVES,
};

constexpr uint32_t kPopupMs = 1200;
constexpr uint32_t kStoreQuietMs = 1500;
constexpr int kJoyStep = 4;          // of 64 from the center to an edge, per detent
constexpr int kWavePages = 6;        // wave A-D, mix, store

const char* const kFxName[5] = {"Prog", "Off", "Chorus", "Ensemble", "VS Chorus"};
const char* const kArpName[3] = {"Off", "On", "Latch"};
const char* const kModeName[3] = {"1 Omni", "3 Poly", "4 Mono"};
const char* const kParamsName[4] = {"Off", "Receive", "Transmit", "Both"};
constexpr int kMidiPages = 3;

// The chord sets come with the Pro VS Mini data; without it the switch shows
// "n/a" and stays off.
#if PROVS_HAS_PVM
constexpr bool kHaveChords = true;
#else
constexpr bool kHaveChords = false;
#endif

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

}  // namespace

int VS_Controller::count(uint8_t bank) {
    return bank == kBankMini ? Engine::pvmPatchCount() : Engine::presetCount();
}

void VS_Controller::init(const uint8_t* banks, size_t size) {
    banks_ = banks;
    banksSize_ = size;
    if (size) {
        Engine::loadVsSysex(banks, size);
        Engine::loadPvmSysex(banks, size);
    }
    store_.load();          // a stored bank replaces the embedded one
    store_.loadWaves();
    while (Engine::buildUserWaves(1000)) {}      // nothing sounds yet: all at once
    applyArp();
    applyMidi();
    select(kBankVs, 0);
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------
void VS_Controller::select(uint8_t bank, int program) {
    if (bank == kBankMini && count(kBankMini) == 0) bank = kBankVs;
    const int n = count(bank);
    bank_ = bank;
    program_ = (uint8_t)clampInt(program, 0, n - 1);
    if (bank_ == kBankVs) memcpy(edit_, Engine::vsProgram(program_), sizeof edit_);
    edited_ = false;
    comparing_ = false;
    play();
}

void VS_Controller::play() {
    if (state_ == State::Wave) { playWaveEdit(); return; }
    if (bank_ == kBankMini) engine_.setPvmPatch(program_);
    else engine_.setVsProgram(comparing_ ? Engine::vsProgram(program_) : edit_);
    applyOverrides();
}

// Edit Waveform mode does not play the program (owner's manual ch. 13): it
// plays an organ - no modulation, filter open, on and off at once - whose
// four oscillators run the chosen waves at their harmonics, mixed by the
// position on the WAVE MIX page alone. That is, sample for sample, what
// Store writes.
void VS_Controller::playWaveEdit() {
    provs::Params p;
    for (int k = 0; k < 4; ++k) {
        p.osc[k].wave = waveNo_[k];
        p.osc[k].harmonic = waveHarm_[k];
    }
    p.ampEnv.releaseTau = 0.03f;
    p.wheelDest = 0;
    p.mixX = (float)waveX_ * (1.f / 64.f);
    p.mixY = (float)waveY_ * (1.f / 64.f);
    p.gain = 0.7f * (float)volume_ * (1.f / 80.f);
    engine_.setParams(p);
}

// Volume, the joystick, a forced effect and the chord set sit on top of
// whatever the program says.
void VS_Controller::applyOverrides() {
    if (state_ == State::Wave) { playWaveEdit(); return; }
    provs::Params p = engine_.params();
    p.gain = 0.7f * (float)volume_ * (1.f / 80.f);      // 80 = the engine's own level
    p.mixX = (float)joyX_ * (1.f / 64.f);
    p.mixY = (float)joyY_ * (1.f / 64.f);
    if (fx_ > 0) p.fxType = (provs::Fx::Type)(fx_ - 1);
    p.chordSet = kHaveChords ? chord_ : 0;
    engine_.setParams(p);
}

void VS_Controller::applyArp() {
    engine_.setArpClocksPerStep(clocks_);
    engine_.setArpExternalClock(arpClock_ != 0);
    engine_.setArpLatch(arp_ == 2);
    engine_.setArp(arp_ != 0);
}

void VS_Controller::setVolume(uint8_t volume) {
    volume_ = volume > 100 ? 100 : volume;
    applyOverrides();
}

// ---------------------------------------------------------------------------
// MIDI in
// ---------------------------------------------------------------------------
bool VS_Controller::target(uint8_t ch, Target& t) const {
    const bool two = engine_.params().keyMode != provs::kKeyNormal;
    t = {Engine::kAllVoices, -1, -1, false};
    ch &= 15;
    if (midiMode_ == 0) return true;                         // mode 1: any channel
    if (midiMode_ == 1) {                                    // mode 3
        if (!two || leftChannel_ == rightChannel_) return ch == rightChannel_;
        if (ch == rightChannel_) { t.layer = 0; t.voices = engine_.layerVoices(0); return true; }
        if (ch == leftChannel_)  { t.layer = 1; t.voices = engine_.layerVoices(1); return true; }
        return false;
    }
    // Mode 4: one voice per channel, wrapping past 16; where the two ranges
    // of a split overlap, the right program has the channel
    const int n = two ? 4 : 8;
    int d = (ch - rightChannel_) & 15;
    if (d < n) { t.voice = d; t.voices = 1u << d; return true; }
    if (two) {
        d = (ch - leftChannel_) & 15;
        if (d < 4) { t.voice = 4 + d; t.voices = 1u << (4 + d); return true; }
    }
    // the channel below a range is its global channel
    if (ch == ((rightChannel_ - 1) & 15)) { t.voices = engine_.layerVoices(0); t.global = true; return true; }
    if (two && ch == ((leftChannel_ - 1) & 15)) { t.voices = engine_.layerVoices(1); t.global = true; return true; }
    return false;
}

bool VS_Controller::noteOn(uint8_t ch, uint8_t note, uint8_t velocity) {
    Target t;
    if (!target(ch, t) || t.global) return false;
    if (transposeArmed_) {               // Latch/Transpose held and a key: relative to middle C
        transposeArmed_ = false;
        transpose_ = (int8_t)((int)note - 60);
        engine_.setArpTranspose(transpose_);
        snprintf(popupText_, sizeof popupText_, "Tr %+d", (int)transpose_);
        popup(popupText_);
        return true;
    }
    if (t.voice >= 0) engine_.voiceOn(t.voice, note, velocity);
    else if (t.layer >= 0) engine_.layerOn(t.layer, note, velocity);
    else engine_.noteOn(note, velocity);
    return true;
}

bool VS_Controller::noteOff(uint8_t ch, uint8_t note) {
    Target t;
    if (!target(ch, t) || t.global) return false;
    if (t.voice >= 0) engine_.voiceOff(t.voice, note);
    else if (t.layer >= 0) engine_.layerOff(t.layer, note);
    else engine_.noteOff(note);
    return true;
}

// Controllers as the VS takes them (owner's manual ch. 14, table @0x4FD2).
// Hold, which the VS does not have, is on 66.
void VS_Controller::controlChange(uint8_t ch, uint8_t cc, uint8_t value) {
    Target t;
    if (!target(ch, t)) return;
    const float v = (float)value * (1.f / 127.f);
    switch (cc) {
    case 32:  midiBank_ = value; break;                              // bank select LSB
    case 1:                                                          // mod wheel
    case 4:   engine_.setModWheel(v, t.voices); break;               // foot pedal: the same
    case 2:   engine_.setPressure(v, t.voices); break;               // breath: as pressure
    case 7:   setVolume((uint8_t)((value * 100 + 63) / 127)); break;
    case 16:                                                         // joystick x, y: 64 = center
    case 17:  engine_.setJoystick(cc - 16, value >= 127 ? 1.f : ((float)value - 64.f) * (1.f / 64.f), t.voices); break;
    case 64:                                                         // the alternate-release footswitch
    case 69:  engine_.setAltRelease(value >= 64); break;             // its second number
    case 66:  engine_.setHold(value >= 64); break;
    case 98:  paramLsb_ = value; break;                              // parameter select LSB, MSB
    case 99:  paramMsb_ = value; break;
    case 38:  dataLsb_ = value; break;                               // data entry LSB
    case 6: {                                                        // data entry MSB: the slider position
        if (!(midiParams_ & 1) || bank_ != kBankVs || comparing_) break;
        const vs::ParamDef* p = vs::paramByMidi((paramMsb_ << 7) | paramLsb_);
        // @0x50BE: eight bits, the top one of the LSB below the seven of the MSB
        if (p && vs::paramSetSlider(*p, edit_, (value << 1) | (dataLsb_ >> 6))) {
            edited_ = true;
            play();
        }
        dataLsb_ = 0;
        break;
    }
    case 120:
    case 123: engine_.allNotesOff(); break;
    default:  break;
    }
}

void VS_Controller::pitchBend(uint8_t ch, int16_t bend) {
    Target t;
    if (target(ch, t)) engine_.pitchBend((float)bend * (1.f / 8192.f), t.voices);
}

void VS_Controller::pressure(uint8_t ch, uint8_t value) {
    Target t;
    if (target(ch, t)) engine_.setPressure((float)value * (1.f / 127.f), t.voices);
}

// Bank select LSB 0 = Prophet VS programs, 1 = Pro VS Mini patches. On the
// left channel of a split or double it changes the linked program only.
void VS_Controller::programChange(uint8_t ch, uint8_t program) {
    Target t;
    if (!target(ch, t)) return;
    if (t.layer == 1 || (t.voice >= 4 && engine_.params().keyMode != provs::kKeyNormal)) {
        if (bank_ == kBankVs && program < 100 && !comparing_) { edit_[61] = program; edited_ = true; play(); }
        return;
    }
    const uint8_t bank = midiBank_ == 1 ? kBankMini : kBankVs;
    if (program >= count(bank)) return;          // past the end: ignored, not wrapped
    select(bank, program);
}

// ---------------------------------------------------------------------------
// MIDI out
// ---------------------------------------------------------------------------
void VS_Controller::sendChannelMessage(uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t m[3] = {status, (uint8_t)(d1 & 127), (uint8_t)(d2 & 127)};
    if (vs_sysex_room() >= sizeof m && !dump_.busy()) vs_sysex_write(m, sizeof m);
}

// A parameter changed on the panel, as the VS sends it: select LSB and MSB,
// then the slider position as data entry LSB and MSB.
void VS_Controller::sendParam(int index) {
    const vs::ParamDef& p = vs::param(index);
    const int number = vs::paramMidiNumber(p);
    if (number < 0) return;
    const int pos = vs::paramSlider(p, edit_);
    const uint8_t cc = (uint8_t)(0xB0 | rightChannel_);
    sendChannelMessage(cc, 98, (uint8_t)(number & 127));
    sendChannelMessage(cc, 99, (uint8_t)(number >> 7));
    sendChannelMessage(cc, 38, (uint8_t)((pos & 1) << 6));
    sendChannelMessage(cc, 6, (uint8_t)(pos >> 1));
}

void VS_Controller::applyMidi() {
    engine_.setArpOut(arpChannel_ != 0);
    engine_.setOverflow(overflow_);
}

void VS_Controller::editStep(int which, int delta) {
    const int i = cursor_ + which;
    if (i >= vs::paramCount() || comparing_) return;     // compare shows the stored version
    if (vs::paramStep(vs::param(i), edit_, delta)) {
        edited_ = true;
        play();
        if (midiParams_ & 2) sendParam(i);
    }
}

void VS_Controller::write(int slot) {
    Engine::storeVsProgram(slot, edit_);
    store_.touch(slot);
    memChangedMs_ = nowMs_;
    select(kBankVs, slot);
}

// The mix on the WAVE MIX page becomes user wave 'waveTo_', computed as the
// firmware computes it.
void VS_Controller::storeWave() {
    const int wave[4] = {waveNo_[0], waveNo_[1], waveNo_[2], waveNo_[3]};
    const int harm[4] = {waveHarm_[0], waveHarm_[1], waveHarm_[2], waveHarm_[3]};
    int16_t out[Engine::kWaveSamples];
    Engine::mixWave(wave, harm, (float)waveX_ * (1.f / 64.f), (float)waveY_ * (1.f / 64.f), out);
    Engine::setUserWave(waveTo_, out);
    store_.touchWaves();
    memChangedMs_ = nowMs_;
}

void VS_Controller::factoryReset() {
    Engine::resetVsBank();
    Engine::resetPvmBank();
    Engine::clearUserWaves();
    if (banksSize_) {
        Engine::loadVsSysex(banks_, banksSize_);
        Engine::loadPvmSysex(banks_, banksSize_);
    }
    store_.touchAll();
    store_.touchWaves();
    memChangedMs_ = nowMs_;
    engine_.allNotesOff();
    select(kBankVs, 0);
}

// ---------------------------------------------------------------------------
// SysEx
// ---------------------------------------------------------------------------
void VS_Controller::sysExMessage(const uint8_t* data, size_t length) {
    if (length < 4 || data[0] != 0xF0) return;

    if (data[1] == 0x7E) {                               // sample dump standard
        const int wave = dump_.sampleDump(data, length, channel(), nowMs_);
        if (wave >= 0) {
            Engine::setUserWave(wave, dump_.received());
            store_.touchWaves();
            memChangedMs_ = nowMs_;
            popup("Loaded");
        }
        return;
    }
    if (data[1] != 0x01) return;                         // not Sequential

    if (data[2] == 0x00) {                               // dump request (@0x4BC6)
        if (length < 5) return;
        const uint8_t nn = data[3];
        if (nn == 0x7F) dump_.sendWaves();
        else if (nn == 0x64) dump_.sendBank();
        else if (nn < 100) dump_.sendProgram(Engine::vsProgram(nn), nn);
        return;
    }
    if (data[2] == 0x0A && data[3] == 0x7F) {            // all user waves
        receiveWaves(data, length);
        return;
    }

    const int nVs = Engine::loadVsSysex(data, length);
    const int nMini = Engine::loadPvmSysex(data, length);
    if (nVs + nMini == 0) return;
    if (nVs) {
        store_.touchAll();
        memChangedMs_ = nowMs_;
    }
    engine_.allNotesOff();
    select(bank_, program_);
    popup("Loaded");
}

// F0 01 0A 7F, 32 records of 192 bytes as nibbles, F7. A record that is all
// zero leaves its slot as the build has it: that is how a slot never written
// goes out, and silence is not worth a slot.
void VS_Controller::receiveWaves(const uint8_t* data, size_t length) {
    constexpr size_t kNibbles = 2 * Engine::kWaveRecordBytes;
    if (length != 4 + Engine::kUserWaves * kNibbles + 1) return;
    for (size_t i = 4; i + 1 < length; ++i)
        if (data[i] > 15) return;
    for (int w = 0; w < Engine::kUserWaves; ++w) {
        const uint8_t* nib = data + 4 + (size_t)w * kNibbles;
        uint8_t record[Engine::kWaveRecordBytes];
        for (int k = 0; k < Engine::kWaveRecordBytes; ++k)
            record[k] = (uint8_t)((nib[2 * k] << 4) | nib[2 * k + 1]);
        int16_t samples[Engine::kWaveSamples];
        Engine::unpackWave(record, samples);
        bool any = false;
        for (int16_t v : samples) any |= v != 0;
        if (any) Engine::setUserWave(w, samples); else Engine::clearUserWave(w);
    }
    store_.touchWaves();
    memChangedMs_ = nowMs_;
    popup("Loaded");
}

bool VS_Controller::poll(uint32_t nowMs) {
    nowMs_ = nowMs;
    bool dirty = false;

    if (dump_.poll(nowMs)) {
        popup("Sent");
        dirty = true;
    }

    // Notes the engine hands on: the arpeggiator's on its channel, overflow
    // on the right channel
    Engine::NoteOut e;
    while (engine_.popNoteOut(e)) {
        const uint8_t ch = e.arp ? (uint8_t)(arpChannel_ - 1) : rightChannel_;
        if (e.arp && arpChannel_ == 0) continue;
        sendChannelMessage((uint8_t)((e.on ? 0x90 : 0x80) | (ch & 15)), e.note, e.on ? e.velocity : 64);
    }

    // The levels of a user wave that has changed: a few harmonics a tick, so
    // that the audio next door does not notice.
    Engine::buildUserWaves(8);

    // The flash mirror: written once things have been quiet for a while and
    // nothing sounds, since the erase stops the CPU.
    if (store_.dirty() && (nowMs - memChangedMs_) > kStoreQuietMs && engine_.activeVoices() == 0 &&
        !sending()) {
        store_.store();
    }
    return dirty;
}

void VS_Controller::setLoad(float percent) {
    load_ = percent;
    if (percent > loadPeak_) loadPeak_ = percent;
}

void VS_Controller::popup(const char* text) {
    popup_ = text;
    popupUntil_ = nowMs_ + kPopupMs;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void VS_Controller::openMenu() {
    nMenu_ = 0;
    auto item = [&](const char* s, uint8_t a) { menu_[nMenu_] = s; menuAction_[nMenu_] = a; ++nMenu_; };
    if (bank_ == kBankVs) {          // the Mini's patches are played, not edited
        item("Edit Program", A_EDIT);
        item("Write Program", A_WRITE);
        item(comparing_ ? "Compare: Off" : "Compare", A_COMPARE);
    }
    item("Edit Waveform", A_WAVE);
    item("MIDI Options", A_MIDI);
    item("Cartridge", A_CART);
    item("Send SysEx", A_SEND);
    item("Factory Reset", A_RESET);
    item("<< Back", A_EXIT);
    list_.open(menu_, (uint8_t)nMenu_, 0);
    state_ = State::Menu;
}

void VS_Controller::openSend() {
    nMenu_ = 0;
    auto item = [&](const char* s, uint8_t a) { menu_[nMenu_] = s; menuAction_[nMenu_] = a; ++nMenu_; };
    if (bank_ == kBankVs) item("This Program", A_SEND_PROGRAM);
    item("VS Bank", A_SEND_BANK);
    item("User Waves", A_SEND_WAVES);
    item("<< Back", A_BACK);
    list_.open(menu_, (uint8_t)nMenu_, 0);
    state_ = State::Send;
}

void VS_Controller::openCart() {
    nMenu_ = 0;
    auto item = [&](const char* s, uint8_t a) { menu_[nMenu_] = s; menuAction_[nMenu_] = a; ++nMenu_; };
    item("Save Programs", A_CART_SAVE_PROGS);
    item("Save Waves", A_CART_SAVE_WAVES);
    item("Load Programs", A_CART_LOAD_PROGS);
    item("Load Waves", A_CART_LOAD_WAVES);
    item("<< Back", A_BACK);
    list_.open(menu_, (uint8_t)nMenu_, 0);
    state_ = State::Cart;
}

void VS_Controller::menuChoose(int i) {
    switch (menuAction_[i]) {
    case A_EDIT:
        state_ = State::Edit;
        break;
    case A_WRITE:
        writeTo_ = program_;
        state_ = State::Write;
        break;
    case A_COMPARE:
        comparing_ = !comparing_;
        state_ = State::Play;
        play();
        break;
    case A_WAVE:
        state_ = State::Wave;
        wavePage_ = 0;
        engine_.allNotesOff();
        play();
        break;
    case A_MIDI:
        state_ = State::Midi;
        midiPage_ = 0;
        break;
    case A_CART:
        openCart();
        break;
    case A_SEND:
        openSend();
        break;
    case A_RESET:
        state_ = State::Confirm;
        break;
    default:
        state_ = State::Play;
        break;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool VS_Controller::input(const picoface::ui::InputState& raw) {
    nowMs_ = raw.nowMs;
    // SEL carries two gestures. The core reports the press on the way down
    // and the long press after a while, so a long press would first act as a
    // short one and then land in whatever that opened. A short press
    // therefore counts on release, and only if no long press fired while the
    // button was held.
    const bool down = raw.down(Button::Sel);
    if (raw.longPress(Button::Sel)) selLongSeen_ = true;
    const bool click = !down && selWasDown_ && !selLongSeen_;
    if (!down) selLongSeen_ = false;
    selWasDown_ = down;
    picoface::ui::InputState in = raw;
    in.buttonPressed[(int)Button::Sel] = click;      // what ListView sees, too
    const int ds = in.delta(Encoder::Sel), da = in.delta(Encoder::ParamA), db = in.delta(Encoder::ParamB);
    const bool selShort = click, selLong = raw.longPress(Button::Sel);
    const bool aPress = raw.pressed(Button::ParamA), bPress = raw.pressed(Button::ParamB);
    bool dirty = ds || da || db || selShort || selLong || aPress || bPress;
    if (popup_ && in.nowMs > popupUntil_) { popup_ = nullptr; dirty = true; }

    switch (state_) {
    case State::Play:
        if (selLong) { openMenu(); break; }
        if (ds) {
            const int n = (int)Page::Count;
            int p = ((int)page_ + ds) % n;
            if (p < 0) p += n;
            page_ = (Page)p;
        }
        switch (page_) {
        case Page::Program:
            if (da) {
                const int n = count(bank_);
                int p = ((int)program_ + da) % n;
                if (p < 0) p += n;
                select(bank_, p);
            }
            if (db) {
                const uint8_t b = db > 0 ? kBankMini : kBankVs;
                if (b != bank_ && count(b) > 0) select(b, 0);
            }
            break;
        case Page::Level:
            if (da) setVolume((uint8_t)clampInt(volume_ + da * 2, 0, 100));
            if (db) {
                fx_ = (uint8_t)clampInt(fx_ + db, 0, 4);
                play();                  // "Prog" needs the programmed effect back
            }
            break;
        case Page::Joystick:
            // A moves between A and C, B between D and B: clockwise is right, and up
            if (da) joyX_ = (int8_t)clampInt(joyX_ + da * kJoyStep, -64, 64);
            if (db) joyY_ = (int8_t)clampInt(joyY_ - db * kJoyStep, -64, 64);
            if (aPress || bPress) joyX_ = joyY_ = 0;
            if (da || db || aPress || bPress) applyOverrides();
            break;
        case Page::Arp:
            if (da) { arp_ = (uint8_t)clampInt(arp_ + da, 0, 2); applyArp(); }
            // The VS transposes a latched arpeggio with Latch/Transpose held
            // and a key. Here: press A, then play the key (middle C = none);
            // press B to take the transposition back.
            if (aPress) { transposeArmed_ = !transposeArmed_; popup(transposeArmed_ ? "Key?" : "Tr off"); }
            if (bPress) { transposeArmed_ = false; transpose_ = 0; engine_.setArpTranspose(0); popup("Tr +0"); }
            // Layer is a parameter of the program (field 94), as on the VS
            if (db && bank_ == kBankVs && !comparing_) {
                const uint8_t on = db > 0 ? 1 : 0;
                if (edit_[94] != on) { edit_[94] = on; edited_ = true; play(); }
            }
            break;
        case Page::Arp2:
            if (da) { clocks_ = (uint8_t)clampInt(clocks_ + da, 1, 24); applyArp(); }
            if (db && kHaveChords) { chord_ = (uint8_t)clampInt(chord_ + db, 0, 8); applyOverrides(); }
            break;
        case Page::Midi:
            if (da) midiMode_ = (uint8_t)clampInt(midiMode_ + da, 0, 2);
            if (db) rightChannel_ = (uint8_t)clampInt(rightChannel_ + db, 0, 15);
            if (da || db) engine_.allNotesOff();   // a note of the old channel would never end
            break;
        case Page::Diag:
            if (bPress) loadPeak_ = 0.f;
            break;
        default:
            break;
        }
        break;

    case State::Menu: {
        if (selLong) { state_ = State::Play; break; }
        const int c = list_.update(in);
        if (c != picoface::ui::ListView::kNone) menuChoose(c);
        break;
    }
    case State::Send: {
        if (selLong) { state_ = State::Play; break; }
        const int c = list_.update(in);
        if (c == picoface::ui::ListView::kNone) break;
        bool started = false;
        switch (menuAction_[c]) {
        case A_SEND_PROGRAM: started = dump_.sendProgram(edit_, program_); break;
        case A_SEND_BANK:    started = dump_.sendBank(); break;
        case A_SEND_WAVES:   started = dump_.sendWaves(); break;
        default:             break;
        }
        if (menuAction_[c] != A_BACK) popup(started ? "Sending" : "Busy");
        state_ = State::Play;
        break;
    }
    case State::Edit:
        if (selLong) { state_ = State::Play; break; }
        if (selShort) {
            list_.open(vs::groupNames(), (uint8_t)vs::groupCount(), (uint8_t)vs::groupOf(cursor_));
            state_ = State::Groups;
            break;
        }
        if (ds) cursor_ = clampInt(cursor_ + 2 * ds, 0, (vs::paramCount() - 1) & ~1);
        if (da) editStep(0, da);
        if (db) editStep(1, db);
        break;
    case State::Groups: {
        if (selLong) { state_ = State::Edit; break; }
        const int c = list_.update(in);
        if (c != picoface::ui::ListView::kNone) {
            cursor_ = vs::groupFirst(c);
            state_ = State::Edit;
        }
        break;
    }
    case State::Write:
        if (selLong) { state_ = State::Play; break; }
        if (da) writeTo_ = clampInt(writeTo_ + da, 0, 99);
        if (selShort) {
            write(writeTo_);
            popup("Written");
            state_ = State::Play;
        }
        break;
    case State::Confirm:
        if (selLong || da < 0) { state_ = State::Play; break; }
        if (selShort) {
            factoryReset();
            popup("Reset");
            state_ = State::Play;
        }
        break;
    case State::Midi:
        if (selLong) { state_ = State::Play; break; }
        if (ds) {
            int p = ((int)midiPage_ + ds) % kMidiPages;
            if (p < 0) p += kMidiPages;
            midiPage_ = (uint8_t)p;
        }
        if (midiPage_ == 0) {
            if (da) { leftChannel_ = (uint8_t)clampInt(leftChannel_ + da, 0, 15); engine_.allNotesOff(); }
            if (db) arpChannel_ = (uint8_t)clampInt(arpChannel_ + db, 0, 16);
        } else if (midiPage_ == 1) {
            if (da) midiParams_ = (uint8_t)clampInt(midiParams_ + da, 0, 3);
            if (db) overflow_ = db > 0;
        } else if (da) {
            arpClock_ = da > 0 ? 1 : 0;
            applyArp();
        }
        applyMidi();
        break;
    case State::Cart: {
        if (selLong) { state_ = State::Play; break; }
        const int c = list_.update(in);
        if (c == picoface::ui::ListView::kNone) break;
        const uint8_t a = menuAction_[c];
        state_ = State::Play;
        if (a == A_BACK) break;
        engine_.allNotesOff();           // the flash write stops the CPU
        if (a == A_CART_SAVE_PROGS || a == A_CART_SAVE_WAVES) {
            popup(store_.saveCartridge(a == A_CART_SAVE_PROGS, a == A_CART_SAVE_WAVES) ? "Saved" : "Failed");
        } else if (store_.loadCartridge(a == A_CART_LOAD_PROGS, a == A_CART_LOAD_WAVES)) {
            memChangedMs_ = nowMs_;
            select(bank_, program_);
            popup("Loaded");
        } else {
            popup("Empty");
        }
        break;
    }
    case State::Wave:
        if (selLong) {                   // back to the program
            state_ = State::Play;
            engine_.allNotesOff();
            play();
            break;
        }
        if (ds) {
            int p = ((int)wavePage_ + ds) % kWavePages;
            if (p < 0) p += kWavePages;
            wavePage_ = (uint8_t)p;
        }
        if (wavePage_ < 4) {
            if (da) waveNo_[wavePage_] = (uint8_t)clampInt(waveNo_[wavePage_] + da, 0, 126);
            if (db) waveHarm_[wavePage_] = (uint8_t)clampInt(waveHarm_[wavePage_] + db, 1, 32);
            if (da || db) playWaveEdit();
        } else if (wavePage_ == 4) {
            if (da) waveX_ = (int8_t)clampInt(waveX_ + da * kJoyStep, -64, 64);
            if (db) waveY_ = (int8_t)clampInt(waveY_ - db * kJoyStep, -64, 64);
            if (aPress || bPress) waveX_ = waveY_ = 0;
            if (da || db || aPress || bPress) playWaveEdit();
        } else {
            if (da) waveTo_ = (uint8_t)clampInt(waveTo_ + da, 0, Engine::kUserWaves - 1);
            if (selShort) {
                storeWave();
                popup("Stored");
            }
        }
        break;
    }
    return dirty;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
bool VS_Controller::draw(picoface::ui::Display& d, uint32_t nowMs) {
    bool marquee = false;
    switch (state_) {
    case State::Play:   marquee = drawPlay(d, nowMs); break;
    case State::Menu:   list_.draw(d, bank_ == kBankVs ? "PROGRAM" : "MINI PATCH"); break;
    case State::Send:   list_.draw(d, "SEND SYSEX"); break;
    case State::Groups: list_.draw(d, "PAGES"); break;
    case State::Edit:   drawEdit(d); break;
    case State::Write:  drawWrite(d); break;
    case State::Wave:   drawWave(d); break;
    case State::Midi:   drawMidi(d); break;
    case State::Cart:   list_.draw(d, "CARTRIDGE"); break;
    case State::Confirm:
        d.clear();
        kit::popup(d, "FACTORY RESET?", "SEL: yes");   // a long press backs out
        break;
    }
    if (popup_ && (state_ == State::Play || state_ == State::Wave)) kit::popup(d, "VS", popup_);
    d.flush();
    return marquee;
}

// The vector plane as on the VS's panel - A left, B top, C right, D bottom -
// with the position as the mix sees it, and the four shares it gives. x runs
// toward C, y toward D; a position outside the diamond counts as the nearest
// point of its edge, which is what the mix law makes of it (@0xB4E).
void VS_Controller::drawVector(picoface::ui::Display& d, int x, int y) {
    constexpr int cx = 27, cy = 38, r = 17;
    const float fx = (float)x * (1.f / 64.f), fy = (float)y * (1.f / 64.f);
    const float u = clamp01(0.5f * (fx + fy) + 0.5f), w = clamp01(0.5f * (fy - fx) + 0.5f);
    const int share[4] = {(int)((1.f - u) * w * 100.f + 0.5f), (int)((1.f - u) * (1.f - w) * 100.f + 0.5f),
                          (int)(u * (1.f - w) * 100.f + 0.5f), (int)(u * w * 100.f + 0.5f)};

    u8g2_t* g = d.raw();
    u8g2_DrawLine(g, cx - r, cy, cx, cy - r);
    u8g2_DrawLine(g, cx, cy - r, cx + r, cy);
    u8g2_DrawLine(g, cx + r, cy, cx, cy + r);
    u8g2_DrawLine(g, cx, cy + r, cx - r, cy);
    const int px = cx + (int)((u - w) * (float)r + (u >= w ? 0.5f : -0.5f));
    const int py = cy + (int)((u + w - 1.f) * (float)r + (u + w >= 1.f ? 0.5f : -0.5f));
    u8g2_DrawBox(g, (u8g2_uint_t)(px - 1), (u8g2_uint_t)(py - 1), 3, 3);

    d.setFont(u8g2_font_4x6_tf);
    d.drawText(cx - r - 6, cy + 3, "A");
    d.drawText(cx - 1, cy - r - 2, "B");
    d.drawText(cx + r + 3, cy + 3, "C");
    d.drawText(cx - 1, 63, "D");
    d.drawText(60, 53, "A: left/right");
    d.drawText(60, 61, "B: up/down");

    char line[16];
    d.setFont(u8g2_font_6x12_tf);
    snprintf(line, sizeof line, "A%-3d B%d", share[0], share[1]);
    d.drawText(60, 27, line);
    snprintf(line, sizeof line, "C%-3d D%d", share[2], share[3]);
    d.drawText(60, 40, line);
}

bool VS_Controller::drawPlay(picoface::ui::Display& d, uint32_t nowMs) {
    static const char* const kTitle[] = {"PROGRAM", "LEVEL", "JOYSTICK", "ARP", "ARP / CHORD", "MIDI", "DIAG"};
    char ta[24] = {0}, tb[24] = {0};
    d.clear();

    if (page_ == Page::Diag) {
        char r0[24], r1[24], r2[24];
        snprintf(r0, sizeof r0, "Voices %d", engine_.activeVoices());
        snprintf(r1, sizeof r1, "CPU %d%%  peak %d%%", (int)load_, (int)loadPeak_);
        snprintf(r2, sizeof r2, "Memory %s", store_.dirty() ? "not saved yet" : "saved");
        const char* rows[] = {r0, r1, r2, "B: reset peak"};
        kit::diagnostics(d, "DIAG", rows, 4);
        return false;
    }

    kit::header(d, kTitle[(int)page_], (int)page_, (int)Page::Count);
    switch (page_) {
    case Page::Program: {
        char number[8], name[12], sub[28];
        if (bank_ == kBankMini) {
            snprintf(number, sizeof number, "M%02d", (int)program_ + 1);
            snprintf(name, sizeof name, "%s", Engine::pvmPatchName(program_));
        } else {
            snprintf(number, sizeof number, "%02d%s", (int)program_, edited_ ? "*" : "");
            for (int i = 0; i < 8; ++i) name[i] = provs::kNameChars[edit_[78 + i] & 31];
            name[8] = 0;
        }
        // What is switched on over the program: compare, arpeggiator, a
        // forced effect.
        snprintf(sub, sizeof sub, "%s%s%s%s", comparing_ ? "CMP  " : "",
                 arp_ ? (arp_ == 2 ? "Arp Latch  " : "Arp  ") : "", fx_ ? "FX " : "",
                 fx_ ? kFxName[fx_] : "");
        kit::Param b;
        b.name = "Bank";
        b.text = bank_ == kBankMini ? "Mini" : "VS";
        b.dimmed = count(kBankMini) == 0;
        return kit::panelName(d, number, name, sub, b, nowMs);
    }
    case Page::Level:
        snprintf(ta, sizeof ta, "%d", (int)volume_);
        kit::panelDuo(d, {"Volume", ta, (float)volume_ * 0.01f}, {"Effect", kFxName[fx_], -1});
        break;
    case Page::Joystick:
        drawVector(d, joyX_, joyY_);
        break;
    case Page::Arp: {
        kit::Param b{"Layer", "n/a", -1};
        if (bank_ == kBankVs) b.text = (comparing_ ? Engine::vsProgram(program_) : edit_)[94] ? "On" : "Off";
        else b.dimmed = true;
        kit::panelDuo(d, {"Arp", kArpName[arp_], -1}, b);
        break;
    }
    case Page::Arp2: {
        snprintf(ta, sizeof ta, "%d", (int)clocks_);
        if (!kHaveChords) snprintf(tb, sizeof tb, "n/a");
        else if (chord_ == 0) snprintf(tb, sizeof tb, "Off");
        else snprintf(tb, sizeof tb, "%d", (int)chord_);
        kit::Param b{"Chord", tb, -1};
        b.dimmed = !kHaveChords;
        kit::panelDuo(d, {"Clk/Step", ta, (float)(clocks_ - 1) / 23.f}, b);
        break;
    }
    case Page::Midi:
        snprintf(tb, sizeof tb, "%d", (int)rightChannel_ + 1);
        kit::panelDuo(d, {"Mode", kModeName[midiMode_], -1}, {"Channel", tb, (float)rightChannel_ / 15.f});
        break;
    default:
        break;
    }
    return false;
}

void VS_Controller::drawEdit(picoface::ui::Display& d) {
    d.clear();
    // No page dots: seventeen groups are past counting. The group list (SEL
    // press) is the map.
    kit::header(d, vs::groupName(vs::groupOf(cursor_)), 0, 0);
    char text[2][16] = {{0}, {0}};
    kit::Param p[2];
    const uint8_t* fields = comparing_ ? Engine::vsProgram(program_) : edit_;
    for (int k = 0; k < 2; ++k) {
        const int i = cursor_ + k;
        if (i >= vs::paramCount()) break;
        const vs::ParamDef& def = vs::param(i);
        if (!def.name) continue;
        float norm = -1.f;
        vs::paramFormat(def, fields, text[k], sizeof text[k], &norm);
        p[k].name = def.name;
        p[k].text = text[k];
        p[k].norm = norm;
    }
    kit::panelDuo(d, p[0], p[1]);
}

void VS_Controller::drawWrite(picoface::ui::Display& d) {
    d.clear();
    kit::header(d, "WRITE PROGRAM", 0, 0);
    char name[9], num[8];
    const uint8_t* old = Engine::vsProgram(writeTo_);    // the init program past the bank's end
    for (int i = 0; i < 8; ++i) name[i] = provs::kNameChars[old[78 + i] & 31];
    name[8] = 0;
    snprintf(num, sizeof num, "%02d", writeTo_);
    kit::panelDuo(d, {"To", num, (float)writeTo_ / 99.f}, {"Replaces", name, -1});
}

void VS_Controller::drawMidi(picoface::ui::Display& d) {
    char ta[16], tb[16];
    d.clear();
    kit::header(d, "MIDI OPTIONS", midiPage_, kMidiPages);
    if (midiPage_ == 0) {
        snprintf(ta, sizeof ta, "%d", (int)leftChannel_ + 1);
        if (arpChannel_) snprintf(tb, sizeof tb, "%d", (int)arpChannel_); else snprintf(tb, sizeof tb, "Off");
        kit::panelDuo(d, {"Left Ch", ta, (float)leftChannel_ / 15.f}, {"Arp Ch", tb, -1});
    } else if (midiPage_ == 1) {
        kit::panelDuo(d, {"Params", kParamsName[midiParams_], -1}, {"Overflow", overflow_ ? "On" : "Off", -1});
    } else {
        kit::panelDuo(d, {"Arp Clock", arpClock_ ? "MIDI" : "Internal", -1}, {"", "", -1});
    }
}

void VS_Controller::drawWave(picoface::ui::Display& d) {
    static const char* const kTitle[kWavePages] = {"WAVE A", "WAVE B", "WAVE C", "WAVE D", "WAVE MIX", "STORE WAVE"};
    char ta[16], tb[16];
    d.clear();
    kit::header(d, kTitle[wavePage_], wavePage_, kWavePages);
    if (wavePage_ < 4) {
        snprintf(ta, sizeof ta, "%d", (int)waveNo_[wavePage_]);
        snprintf(tb, sizeof tb, "%d", (int)waveHarm_[wavePage_]);
        kit::panelDuo(d, {"Wave", ta, (float)waveNo_[wavePage_] / 126.f},
                      {"Harmonic", tb, (float)(waveHarm_[wavePage_] - 1) / 31.f});
    } else if (wavePage_ == 4) {
        drawVector(d, waveX_, waveY_);
    } else {
        // The Review of the VS in one number: how many programs would change
        int users = 0;
        for (int i = 0; i < Engine::presetCount(); ++i) {
            const uint8_t* f = Engine::vsProgram(i);
            if (f[0] == waveTo_ || f[1] == waveTo_ || f[2] == waveTo_ || f[3] == waveTo_) ++users;
        }
        snprintf(ta, sizeof ta, "%02d", (int)waveTo_);
        if (users) snprintf(tb, sizeof tb, "%d prg", users); else snprintf(tb, sizeof tb, "none");
        kit::panelDuo(d, {"To", ta, (float)waveTo_ / 31.f}, {"Used by", tb, -1});
    }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void VS_Controller::exportSettings(VsSettingsV4& s) const {
    s.bank = bank_; s.program = program_; s.fx = fx_; s.volume = volume_;
    s.arp = arp_; s.arpClock = arpClock_; s.clocks = clocks_; s.chord = chord_;
    s.midiMode = midiMode_; s.rightChannel = rightChannel_; s.leftChannel = leftChannel_;
    s.arpChannel = arpChannel_;
    s.joyX = joyX_; s.joyY = joyY_;
    for (int k = 0; k < 4; ++k) { s.waveNo[k] = waveNo_[k]; s.waveHarm[k] = waveHarm_[k]; }
    s.waveX = waveX_; s.waveY = waveY_;
}

void VS_Controller::importSettings(const VsSettingsV4& s) {
    fx_ = s.fx < 5 ? s.fx : 0;
    volume_ = s.volume <= 100 ? s.volume : 80;
    arp_ = s.arp < 3 ? s.arp : 0;
    arpClock_ = s.arpClock ? 1 : 0;
    clocks_ = (uint8_t)clampInt(s.clocks, 1, 24);
    chord_ = s.chord <= 8 ? s.chord : 0;
    midiMode_ = s.midiMode < 3 ? s.midiMode : 0;
    rightChannel_ = s.rightChannel & 15;
    leftChannel_ = s.leftChannel & 15;
    arpChannel_ = s.arpChannel <= 16 ? s.arpChannel : 0;
    applyMidi();
    joyX_ = (int8_t)clampInt(s.joyX, -64, 64);
    joyY_ = (int8_t)clampInt(s.joyY, -64, 64);
    for (int k = 0; k < 4; ++k) {
        waveNo_[k] = (uint8_t)clampInt(s.waveNo[k], 0, 126);
        waveHarm_[k] = (uint8_t)clampInt(s.waveHarm[k], 1, 32);
    }
    waveX_ = (int8_t)clampInt(s.waveX, -64, 64);
    waveY_ = (int8_t)clampInt(s.waveY, -64, 64);
    applyArp();
    select(s.bank == kBankMini ? kBankMini : kBankVs, s.program);
}
