// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// VS_Controller.cpp -- see VS_Controller.h.

#include "VS_Controller.h"

#include <cstdio>
#include <cstring>

#include "picoface/ui_kit.h"
#include "provs/gen/rom_tables.h"   // kNameChars
#include "vs_params.h"

namespace kit = picoface::ui::kit;
using picoface::ui::Button;
using picoface::ui::Encoder;
using provs::Engine;

namespace {

enum MenuAction : uint8_t {
    A_EDIT, A_WRITE, A_COMPARE, A_SEND, A_RESET, A_EXIT,
    A_SEND_PROGRAM, A_SEND_BANK, A_BACK,
};

constexpr uint32_t kPopupMs = 1200;
constexpr uint32_t kStoreQuietMs = 1500;

const char* const kFxName[5] = {"Prog", "Off", "Chorus", "Ensemble", "VS Chorus"};
const char* const kArpName[3] = {"Off", "On", "Latch"};

// The chord sets come with the Pro VS Mini data; without it the switch shows
// "n/a" and stays off.
#if PROVS_HAS_PVM
constexpr bool kHaveChords = true;
#else
constexpr bool kHaveChords = false;
#endif

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
    applyArp();
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
    if (bank_ == kBankMini) engine_.setPvmPatch(program_);
    else engine_.setVsProgram(comparing_ ? Engine::vsProgram(program_) : edit_);
    applyOverrides();
}

// Volume, a forced effect and the chord set sit on top of whatever the
// program says.
void VS_Controller::applyOverrides() {
    provs::Params p = engine_.params();
    p.gain = 0.7f * (float)volume_ * (1.f / 80.f);      // 80 = the engine's own level
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

void VS_Controller::programChange(uint8_t bankLsb, uint8_t program) {
    const uint8_t bank = bankLsb == 1 ? kBankMini : kBankVs;
    if (program >= count(bank)) return;          // past the end: ignored, not wrapped
    select(bank, program);
}

void VS_Controller::setVolume(uint8_t volume) {
    volume_ = volume > 100 ? 100 : volume;
    applyOverrides();
}

void VS_Controller::editStep(int which, int delta) {
    const int i = cursor_ + which;
    if (i >= vs::paramCount() || comparing_) return;     // compare shows the stored version
    if (vs::paramStep(vs::param(i), edit_, delta)) {
        edited_ = true;
        play();
    }
}

void VS_Controller::write(int slot) {
    Engine::storeVsProgram(slot, edit_);
    store_.touch(slot);
    memChangedMs_ = nowMs_;
    select(kBankVs, slot);
}

void VS_Controller::factoryReset() {
    Engine::resetVsBank();
    Engine::resetPvmBank();
    if (banksSize_) {
        Engine::loadVsSysex(banks_, banksSize_);
        Engine::loadPvmSysex(banks_, banksSize_);
    }
    store_.touchAll();
    memChangedMs_ = nowMs_;
    engine_.allNotesOff();
    select(kBankVs, 0);
}

// ---------------------------------------------------------------------------
// SysEx
// ---------------------------------------------------------------------------
void VS_Controller::sysExMessage(const uint8_t* data, size_t length) {
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

bool VS_Controller::poll(uint32_t nowMs) {
    nowMs_ = nowMs;
    bool dirty = false;

    if (txProgram_ && vs_sysex_room() >= (size_t)Engine::kVsDumpBytes) {
        uint8_t msg[Engine::kVsDumpBytes];
        vs_sysex_write(msg, Engine::dumpVsProgram(edit_, program_, msg));
        txProgram_ = false;
    }
    // A bank dump is one message of 16405 bytes: it goes out a program at a
    // time, as the output rings take it.
    if (txBank_ && !txProgram_ && vs_sysex_room() >= 4 + 164 + 1) {
        uint8_t chunk[4 + 164 + 1];
        size_t n = 0;
        if (txNext_ == 0) {
            chunk[n++] = 0xF0; chunk[n++] = 0x01; chunk[n++] = 0x0A; chunk[n++] = 0x64;
        }
        Engine::dumpVsNibbles(Engine::vsProgram(txNext_), chunk + n);
        n += 164;
        if (++txNext_ == 100) {
            chunk[n++] = 0xF7;
            txBank_ = false;
            popup("Sent");
            dirty = true;
        }
        vs_sysex_write(chunk, n);
    }

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
    item("<< Back", A_BACK);
    list_.open(menu_, (uint8_t)nMenu_, 0);
    state_ = State::Send;
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
        play();
        state_ = State::Play;
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
    const bool bPress = raw.pressed(Button::ParamB);
    bool dirty = ds || da || db || selShort || selLong || bPress;
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
        case Page::Arp:
            if (da) arp_ = (uint8_t)clampInt(arp_ + da, 0, 2);
            if (db) arpClock_ = db > 0 ? 1 : 0;
            if (da || db) applyArp();
            break;
        case Page::Arp2:
            if (da) { clocks_ = (uint8_t)clampInt(clocks_ + da, 1, 24); applyArp(); }
            if (db && kHaveChords) { chord_ = (uint8_t)clampInt(chord_ + db, 0, 8); applyOverrides(); }
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
        if (sending()) {
            popup("Busy");
        } else if (menuAction_[c] == A_SEND_PROGRAM) {
            txProgram_ = true;
            popup("Sent");
        } else if (menuAction_[c] == A_SEND_BANK) {
            txBank_ = true;
            txNext_ = 0;
            popup("Sending");
        }
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
    case State::Confirm:
        d.clear();
        kit::popup(d, "FACTORY RESET?", "SEL: yes");   // a long press backs out
        break;
    }
    if (popup_ && state_ == State::Play) kit::popup(d, "VS", popup_);
    d.flush();
    return marquee;
}

bool VS_Controller::drawPlay(picoface::ui::Display& d, uint32_t nowMs) {
    static const char* const kTitle[] = {"PROGRAM", "LEVEL", "ARP", "ARP / CHORD", "DIAG"};
    char ta[24] = {0}, tb[24] = {0};
    d.clear();

    if (page_ == Page::Diag) {
        char r0[24], r1[24], r2[24];
        snprintf(r0, sizeof r0, "Voices %d", engine_.activeVoices());
        snprintf(r1, sizeof r1, "CPU %d%%  peak %d%%", (int)load_, (int)loadPeak_);
        snprintf(r2, sizeof r2, "Bank %s", store_.dirty() ? "not saved yet" : "saved");
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
    case Page::Arp:
        kit::panelDuo(d, {"Arp", kArpName[arp_], -1}, {"Clock", arpClock_ ? "MIDI" : "Internal", -1});
        break;
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

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void VS_Controller::exportSettings(VsSettingsV2& s) const {
    s.bank = bank_; s.program = program_; s.fx = fx_; s.volume = volume_;
    s.arp = arp_; s.arpClock = arpClock_; s.clocks = clocks_; s.chord = chord_;
}

void VS_Controller::importSettings(const VsSettingsV2& s) {
    fx_ = s.fx < 5 ? s.fx : 0;
    volume_ = s.volume <= 100 ? s.volume : 80;
    arp_ = s.arp < 3 ? s.arp : 0;
    arpClock_ = s.arpClock ? 1 : 0;
    clocks_ = (uint8_t)clampInt(s.clocks, 1, 24);
    chord_ = s.chord <= 8 ? s.chord : 0;
    applyArp();
    select(s.bank == kBankMini ? kBankMini : kBankVs, s.program);
}
