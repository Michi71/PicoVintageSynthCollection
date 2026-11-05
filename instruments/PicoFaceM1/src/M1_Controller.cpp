// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include "M1_Controller.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "picoface/ui_kit.h"

using picoface::ui::Button;
using picoface::ui::Encoder;
namespace kit = picoface::ui::kit;

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

enum MenuAction : uint8_t {
    A_EDIT, A_WRITE, A_COMPARE, A_GLOBAL, A_KITS, A_SEND, A_PROTECT, A_RESET, A_EXIT,
    A_SEND_EDIT, A_SEND_PROGS, A_SEND_COMBIS, A_SEND_GLOBAL, A_BACK,
};

constexpr uint32_t kPopupMs = 1200;

// The pseudo parameters at the head of the drum-kit pages.
const m1::ParamDef kKitSel = {"Kit", 0, m1::PK::U, 1, 4, 0};
const m1::ParamDef kInstSel = {"Instrument", 0, m1::PK::U, 1, 30, 0};

}  // namespace

M1_Controller::M1_Controller(M1_Bridge& bridge) : bridge_(bridge) {}

// ---------------------------------------------------------------------------
// Memory, modes
// ---------------------------------------------------------------------------
void M1_Controller::touchMemory(size_t offset, size_t len) {
    bridge_.memory().touch(offset, len);
    memChangedMs_ = nowMs_;
}

void M1_Controller::selectProgram(int n) {
    program_ = (uint8_t)clampInt(n, 0, 99);
    memcpy(progEdit_, bridge_.memory().program(program_), sizeof progEdit_);
    edited_ = comparing_ = false;
    bridge_.playProgram(progEdit_);
}

void M1_Controller::selectCombi(int n) {
    combi_ = (uint8_t)clampInt(n, 0, 99);
    memcpy(combiEdit_, bridge_.memory().combi(combi_), sizeof combiEdit_);
    edited_ = comparing_ = false;
    bridge_.playCombi(combiEdit_);
}

void M1_Controller::applyGlobal() {
    const uint8_t* g = bridge_.memory().image().global;
    // Master tune: the M1's global value plus the PicoFace's own trim.
    bridge_.setMasterTune((int8_t)g[0] + tune_);
}

int M1_Controller::keyTranspose() const {
    return (int8_t)bridge_.memory().image().global[1];
}

void M1_Controller::programChange(uint8_t pc) {
    if (pc > 99) return;
    if (mode_) selectCombi(pc); else selectProgram(pc);
}

int M1_Controller::m1Mode() const {
    const bool editing = state_ == State::Edit || state_ == State::Groups;
    if (editing && (target_ == Target::Global || target_ == Target::Kit)) return 4;
    return mode_ ? (editing ? 1 : 0) : (editing ? 3 : 2);
}

void M1_Controller::setM1Mode(int m) {
    state_ = State::Play;
    if (m == 0 || m == 1) { if (!mode_) { mode_ = 1; selectCombi(combi_); } if (m == 1) buildEdit(Target::Combi), state_ = State::Edit; }
    else if (m == 2 || m == 3) { if (mode_) { mode_ = 0; selectProgram(program_); } if (m == 3) buildEdit(Target::Program), state_ = State::Edit; }
    else if (m == 4) { buildEdit(Target::Global); state_ = State::Edit; }
}

void M1_Controller::programEditLoaded() {
    edited_ = true;
    comparing_ = false;
    if (!mode_) bridge_.playProgram(progEdit_);
    if (state_ == State::Edit && target_ == Target::Program) buildEdit(Target::Program);
}

void M1_Controller::combiEditLoaded() {
    edited_ = true;
    comparing_ = false;
    if (mode_) bridge_.playCombi(combiEdit_);
    if (state_ == State::Edit && target_ == Target::Combi) buildEdit(Target::Combi);
}

bool M1_Controller::writeProgram(int n) {
    if (n < 0 || n > 99 || bridge_.memory().protect) return false;
    memcpy(bridge_.memory().program(n), progEdit_, sizeof progEdit_);
    touchMemory(offsetof(m1::MemoryImage, programs) + 143 * (size_t)n, 143);
    program_ = (uint8_t)n;
    edited_ = comparing_ = false;
    return true;
}

bool M1_Controller::writeCombi(int n) {
    if (n < 0 || n > 99 || bridge_.memory().protect) return false;
    memcpy(bridge_.memory().combi(n), combiEdit_, sizeof combiEdit_);
    touchMemory(offsetof(m1::MemoryImage, combis) + 124 * (size_t)n, 124);
    combi_ = (uint8_t)n;
    edited_ = comparing_ = false;
    return true;
}

void M1_Controller::memoryLoaded() {
    memChangedMs_ = nowMs_;
    bridge_.rebuildKits();
    applyGlobal();
    // The edit buffer follows the memory it came from, as on the M1 after a
    // bank load.
    if (mode_) selectCombi(combi_); else selectProgram(program_);
    if (state_ == State::Edit) buildEdit(target_);
}

// ---------------------------------------------------------------------------
// SysEx parameter change: page and position as in TABLE 5 (program) and
// TABLE 6 (combination, Multi type and the effect pages) of the manual.
// ---------------------------------------------------------------------------
namespace {
// Edit Program pages in the Double numbering; each row = positions A..H.
// 0 = none; bit fields are resolved by the page.
const uint8_t kProgDbl[28][8] = {
    {10, 0, 11, 0, 11, 0, 0, 0},        // 0 OSC basic: mode, assign (C), hold (E)
    {12, 0, 0, 86, 13, 0, 0, 0},        // 1 OSC 1
    {14, 0, 0, 126, 15, 16, 17, 18},    // 2 OSC 2
    {63, 64, 65, 66, 67, 68, 70, 69},   // 3 pitch EG 1
    {103, 104, 105, 106, 107, 108, 110, 109},
    {0, 71, 0, 0, 74, 0, 0, 0},         // 5 VDF 1 cutoff / EG int
    {78, 79, 80, 81, 82, 83, 84, 85},   // 6 VDF 1 EG
    {77, 0, 76, 0, 100, 100, 100, 100}, // 7 VDF 1 velocity
    {72, 73, 75, 0, 99, 99, 99, 99},    // 8 VDF 1 keyboard
    {0, 111, 0, 0, 114, 0, 0, 0},
    {118, 119, 120, 121, 122, 123, 124, 125},
    {117, 0, 116, 0, 140, 140, 140, 140},
    {112, 113, 115, 0, 139, 139, 139, 139},
    {92, 93, 94, 95, 96, 97, 98, 0},    // 13 VDA 1 EG
    {0, 89, 91, 0, 102, 102, 102, 102},
    {87, 88, 90, 0, 101, 101, 101, 101},
    {132, 133, 134, 135, 136, 137, 138, 0},
    {0, 129, 131, 0, 142, 142, 142, 142},
    {127, 128, 130, 0, 141, 141, 141, 141},
    {19, 0, 20, 21, 22, 19, 19, 0},     // 19 pitch MG
    {23, 0, 24, 25, 26, 23, 23, 0},     // 20 VDF MG
    {27, 28, 0, 29, 30, 0, 31, 0},      // 21 after touch
    {32, 33, 0, 34, 35, 0, 36, 37},     // 22 joystick
    {38, 0, 0, 0, 0, 46, 0, 0},         // 23 effect 1 type
    {1, 1, 1, 1, 1, 1, 1, 1},           // 24 effect 1 parameters (*13)
    {39, 0, 0, 0, 0, 46, 0, 0},
    {2, 2, 2, 2, 2, 2, 2, 2},
    {46, 0, 0, 0, 44, 0, 45, 0},        // 27 placement, Out 3/4 pan
};
// Single programs skip the OSC 2 pages: SGL page -> DBL page.
const uint8_t kSglToDbl[19] = {0, 1, 3, 5, 6, 7, 8, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27};
}  // namespace

bool M1_Controller::parameterChange(int page, int pos, int value) {
    pos -= 8;                               // positions A..H are sent as 8..15
    if (pos < 0 || pos > 7) return false;
    if (!mode_) {
        const bool dbl = progEdit_[10] == 1;
        if (!dbl) { if (page < 0 || page > 18) return false; page = kSglToDbl[page]; }
        if (page < 0 || page > 27) return false;
        const uint8_t off = kProgDbl[page][pos];
        if (!off) return false;
        m1::ParamDef p{"", off, m1::PK::U, 0, 255, 0};
        if (off == 1 || off == 2) {                       // effect parameter pages
            p.offset = (uint8_t)(off == 1 ? 47 + pos : 55 + pos);
            p.lo = -128; p.hi = 255;
        } else if (off == 11) { p.kind = m1::PK::Bit; p.aux = pos == 2 ? 0 : 1; p.hi = 1; }
        else if (off == 19 || off == 23) {
            if (pos == 0) { p.kind = m1::PK::Wave; p.hi = 3; }
            else if (pos >= 5) { p.kind = m1::PK::Bit; p.aux = (uint8_t)(pos == 5 ? 5 : 6); p.hi = 1; }
        } else if (off >= 99 && off <= 102) { p.kind = m1::PK::SwPol; p.aux = (uint8_t)(pos - 4); p.hi = 2; }
        else if (off >= 139) { p.kind = m1::PK::SwPol; p.aux = (uint8_t)(pos - 4); p.hi = 2; }
        else if (off == 46) {
            if (page == 27) { p.kind = m1::PK::Placement; p.aux = 4; p.hi = 1; }
            else { p.kind = m1::PK::U; p.hi = 31; }          // effect on/off bits
        } else { p.lo = -128; }
        m1::paramSet(p, progEdit_, value);
        edited_ = true;
        edited(p);
        return true;
    }
    // Combination: page 0 (type), the Multi pages and the effect pages.
    if (page == 0 && pos == 2) { combiEdit_[10] = (uint8_t)clampInt(value, 0, 4); combiEditLoaded(); return true; }
    static const uint8_t kField[15] = {0, 0, 10, 5, 6, 7, 8, 1, 2, 3, 4, 9, 9, 9, 9};
    if (page >= 1 && page <= 14) {
        const uint8_t f = kField[page];
        uint8_t* b = combiEdit_ + 36 + 11 * pos;
        if (page == 2) b[10] = (uint8_t)((b[10] & 0xF0) | (value & 0x0F));
        else if (page >= 11) { const int bit = page - 11; b[9] = (uint8_t)((b[9] & ~(1 << bit)) | ((value & 1) << bit)); }
        else b[f] = (uint8_t)value;
        combiEditLoaded();
        return true;
    }
    if (page >= 15 && page <= 19) {
        uint8_t* fx = combiEdit_ + m1::kCombiFx;
        if (page == 15 && pos == 0) fx[0] = (uint8_t)value;
        else if (page == 17 && pos == 0) fx[1] = (uint8_t)value;
        else if ((page == 15 || page == 17) && pos == 5) fx[8] = (uint8_t)value;
        else if (page == 16) fx[9 + pos] = (uint8_t)value;
        else if (page == 18) fx[17 + pos] = (uint8_t)value;
        else if (page == 19 && pos == 0) fx[8] = (uint8_t)((fx[8] & ~0x10) | ((value & 1) << 4));
        else if (page == 19 && pos == 4) fx[6] = (uint8_t)value;
        else if (page == 19 && pos == 6) fx[7] = (uint8_t)value;
        else return false;
        bridge_.refreshEffects(fx);
        edited_ = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Edit pages
// ---------------------------------------------------------------------------
uint8_t* M1_Controller::editBlock() {
    switch (target_) {
        case Target::Program: return progEdit_;
        case Target::Combi:   return combiEdit_;
        case Target::Global:  return bridge_.memory().image().global;
        case Target::Kit:     return bridge_.memory().image().kits + 210 * kit_ + 7 * kitInst_;
    }
    return progEdit_;
}

void M1_Controller::buildEdit(Target t) {
    target_ = t;
    nParams_ = 0;
    nGroups_ = 0;
    auto group = [&](const char* name) {
        if (nGroups_ >= kMaxGroups) return;
        // Every group starts on the left encoder: a pair never spans two
        // groups. The gap is an empty slot.
        if ((nParams_ & 1) && nParams_ < kMaxParams) {
            params_[nParams_] = m1::ParamDef{nullptr, 0, m1::PK::U, 0, 0, 0};
            paramGroup_[nParams_] = (uint8_t)(nGroups_ - 1);
            ++nParams_;
        }
        snprintf(groupBuf_[nGroups_], sizeof groupBuf_[0], "%s", name);
        groupNames_[nGroups_] = groupBuf_[nGroups_];
        groupFirst_[nGroups_] = (uint8_t)nParams_;
        ++nGroups_;
    };
    auto add = [&](const m1::ParamDef& p, int dOff) {
        if (nParams_ >= kMaxParams || !p.name) return;
        params_[nParams_] = p;
        params_[nParams_].offset = (uint8_t)(p.offset + dOff);
        paramGroup_[nParams_] = (uint8_t)(nGroups_ - 1);
        ++nParams_;
    };
    int n = 0;
    const m1::PageDef* pages = nullptr;
    const uint8_t* blk = editBlock();
    switch (t) {
        case Target::Program: pages = m1::programPages(n); break;
        case Target::Combi:   pages = m1::combiPages(n); break;
        case Target::Global:  pages = m1::globalPages(n); break;
        case Target::Kit:
            group("Kit/Instr");
            add(kKitSel, 0); add(kInstSel, 0);
            pages = m1::kitPages(n);
            break;
    }
    const int fxBase = t == Target::Program ? m1::kProgFx : m1::kCombiFx;
    for (int i = 0; i < n; ++i) {
        const m1::PageDef& pg = pages[i];
        if (t == Target::Program && (pg.flags & m1::kPageDoubleOnly) && blk[10] != 1) continue;
        if (pg.flags & (m1::kPageFx1 | m1::kPageFx2)) {
            const int k = (pg.flags & m1::kPageFx1) ? 0 : 1;
            const int type = blk[fxBase + k];
            char nm[12];
            snprintf(nm, sizeof nm, "FX%d %s", k + 1, m1::kFxNames[type < 34 ? type : 33]);
            group(nm);
            const m1::ParamDef* fp = m1::effectParams(type);
            for (int j = 0; j < 8; ++j) add(fp[j], fxBase + 9 + 8 * k);
            continue;
        }
        if (pg.flags & m1::kPageTimbre) {
            for (int tm = 0; tm < 8; ++tm) {
                char nm[12];
                snprintf(nm, sizeof nm, "Timbre %d", tm + 1);
                group(nm);
                for (int j = 0; j < pg.count; ++j) add(pg.params[j], 11 * tm);
            }
            continue;
        }
        group(pg.title);
        for (int j = 0; j < pg.count; ++j) add(pg.params[j], 0);
    }
    if (cursor_ >= nParams_) cursor_ = 0;
    cursor_ &= ~1;
}

void M1_Controller::edited(const m1::ParamDef& p) {
    switch (target_) {
    case Target::Program:
        if (p.offset >= m1::kProgFx && p.offset < m1::kProgFx + 25) {
            if (!mode_) bridge_.refreshEffects(progEdit_ + m1::kProgFx);
            if (p.offset == m1::kProgFx || p.offset == m1::kProgFx + 1) buildEdit(target_);
        }
        if (p.offset == 10) buildEdit(target_);          // Single <-> Double changes the pages
        break;
    case Target::Combi:
        if (p.offset >= m1::kCombiFx && p.offset < m1::kCombiFx + 25) {
            bridge_.refreshEffects(combiEdit_ + m1::kCombiFx);
            if (p.offset == m1::kCombiFx || p.offset == m1::kCombiFx + 1) buildEdit(target_);
        } else if (p.offset >= 10) {
            bridge_.playCombi(combiEdit_);               // timbre settings are taken at selection
        }
        break;
    case Target::Global:
        touchMemory(0, 21);
        applyGlobal();
        break;
    case Target::Kit:
        touchMemory(21 + 210 * kit_ + 7 * kitInst_, 7);
        bridge_.rebuildKits();
        break;
    }
}

void M1_Controller::editStep(int which, int delta) {
    const int i = cursor_ + which;
    if (i >= nParams_ || !params_[i].name) return;
    const m1::ParamDef& p = params_[i];
    if (target_ == Target::Kit && i < 2) {
        if (i == 0) kit_ = (uint8_t)clampInt(kit_ + delta, 0, 3);
        else kitInst_ = (uint8_t)clampInt(kitInst_ + delta, 0, 29);
        return;
    }
    if (target_ == Target::Program || target_ == Target::Combi) {
        if (comparing_) return;                          // compare shows the stored version
    }
    if (m1::paramStep(p, editBlock(), delta)) {
        if (target_ == Target::Program || target_ == Target::Combi) edited_ = true;
        edited(p);
    }
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void M1_Controller::openMenu() {
    nMenu_ = 0;
    auto item = [&](const char* s, uint8_t a) { menu_[nMenu_] = s; menuAction_[nMenu_] = a; ++nMenu_; };
    item(mode_ ? "Edit Combi" : "Edit Program", A_EDIT);
    item(mode_ ? "Write Combi" : "Write Program", A_WRITE);
    item(comparing_ ? "Compare: Off" : "Compare", A_COMPARE);
    item("Global", A_GLOBAL);
    item("Drum Kits", A_KITS);
    item("Send SysEx", A_SEND);
    item(bridge_.memory().protect ? "Protect: On" : "Protect: Off", A_PROTECT);
    item("Factory Reset", A_RESET);
    item("<< Back", A_EXIT);
    list_.open(menu_, (uint8_t)nMenu_, 0);
    state_ = State::Menu;
}

void M1_Controller::menuChoose(int i) {
    const uint8_t a = menuAction_[i];
    switch (a) {
    case A_EDIT:
        buildEdit(mode_ ? Target::Combi : Target::Program);
        cursor_ = 0;
        state_ = State::Edit;
        break;
    case A_WRITE:
        writeTo_ = mode_ ? combi_ : program_;
        state_ = State::Write;
        break;
    case A_COMPARE:
        comparing_ = !comparing_;
        if (mode_) bridge_.playCombi(comparing_ ? bridge_.memory().combi(combi_) : combiEdit_);
        else bridge_.playProgram(comparing_ ? bridge_.memory().program(program_) : progEdit_);
        state_ = State::Play;
        break;
    case A_GLOBAL: buildEdit(Target::Global); cursor_ = 0; state_ = State::Edit; break;
    case A_KITS:   buildEdit(Target::Kit); cursor_ = 0; state_ = State::Edit; break;
    case A_SEND: {
        nMenu_ = 0;
        auto item = [&](const char* s, uint8_t act) { menu_[nMenu_] = s; menuAction_[nMenu_] = act; ++nMenu_; };
        item(mode_ ? "This Combi" : "This Program", A_SEND_EDIT);
        item("All Programs", A_SEND_PROGS);
        item("All Combis", A_SEND_COMBIS);
        item("Global + Kits", A_SEND_GLOBAL);
        item("<< Back", A_BACK);
        list_.open(menu_, (uint8_t)nMenu_, 0);
        state_ = State::Send;
        break;
    }
    case A_PROTECT:
        bridge_.memory().protect = !bridge_.memory().protect;
        openMenu();
        break;
    case A_RESET:
        confirmAction_ = A_RESET;
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
bool M1_Controller::input(const picoface::ui::InputState& raw) {
    nowMs_ = raw.nowMs;
    // SEL carries two gestures. The core reports the press on the way down
    // and the long press after a while, so a long press would first act as a
    // short one (open the page list, confirm a write) and then the long press
    // would land in whatever that opened. A short press therefore counts on
    // release, and only if no long press fired while the button was held.
    const bool down = raw.down(Button::Sel);
    if (raw.longPress(Button::Sel)) selLongSeen_ = true;
    const bool click = !down && selWasDown_ && !selLongSeen_;
    if (!down) selLongSeen_ = false;
    selWasDown_ = down;
    picoface::ui::InputState in = raw;
    in.buttonPressed[(int)Button::Sel] = click;      // what ListView sees, too
    const int ds = in.delta(Encoder::Sel), da = in.delta(Encoder::ParamA), db = in.delta(Encoder::ParamB);
    const bool selShort = click, selLong = raw.longPress(Button::Sel);
    bool dirty = ds || da || db || selShort || selLong;
    if (popup_ && in.nowMs > popupUntil_) { popup_ = nullptr; dirty = true; }

    switch (state_) {
    case State::Play:
        if (selLong) { openMenu(); break; }
        if (ds) {
            const int n = (int)M1Page::COUNT;
            int p = ((int)page_ + ds) % n;
            if (p < 0) p += n;
            page_ = (M1Page)p;
        }
        if (da) {
            switch (page_) {
            case M1Page::PATCH:
                if (mode_) selectCombi(combi_ + da); else selectProgram(program_ + da);
                break;
            case M1Page::VOLUME: volume_ = (uint8_t)clampInt(volume_ + da, 0, 100); bridge_.setVolume(volume_); break;
            case M1Page::VOICES: voices_ = (uint8_t)clampInt(voices_ + da, 1, m1::kVoices); bridge_.setVoiceLimit(voices_); break;
            case M1Page::TUNE:   tune_ = (int8_t)clampInt(tune_ + da, -50, 50); applyGlobal(); break;
            case M1Page::FX:     fxOn_ = da > 0 ? 1 : 0; bridge_.setFxOn(fxOn_ != 0); break;
            case M1Page::SYS:    midiCh_ = (uint8_t)clampInt(midiCh_ + da, 0, 16); break;
            default: break;
            }
        }
        if (db && page_ == M1Page::PATCH) {
            const uint8_t m = db > 0 ? 1 : 0;
            if (m != mode_) { mode_ = m; if (mode_) selectCombi(combi_); else selectProgram(program_); }
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
        if (sysex_) {
            switch (menuAction_[c]) {
                case A_SEND_EDIT: if (mode_) sysex_->sendCombiEdit(); else sysex_->sendProgramEdit(); break;
                case A_SEND_PROGS: sysex_->sendAllPrograms(); break;
                case A_SEND_COMBIS: sysex_->sendAllCombis(); break;
                case A_SEND_GLOBAL: sysex_->sendGlobal(); break;
                default: break;
            }
            if (menuAction_[c] != A_BACK) { popup_ = "Sending"; popupUntil_ = in.nowMs + kPopupMs; }
        }
        state_ = State::Play;
        break;
    }
    case State::Edit:
        if (selLong) { state_ = State::Play; break; }
        if (selShort) {
            list_.open(groupNames_, (uint8_t)nGroups_, paramGroup_[cursor_ < nParams_ ? cursor_ : 0]);
            state_ = State::Groups;
            break;
        }
        if (ds) cursor_ = clampInt(cursor_ + 2 * ds, 0, (nParams_ - 1) & ~1);
        if (da) editStep(0, da);
        if (db) editStep(1, db);
        break;
    case State::Groups: {
        if (selLong) { state_ = State::Edit; break; }
        const int c = list_.update(in);
        if (c != picoface::ui::ListView::kNone) {
            cursor_ = groupFirst_[c] & ~1;
            if (groupFirst_[c] & 1) cursor_ = groupFirst_[c] - 1;
            state_ = State::Edit;
        }
        break;
    }
    case State::Write:
        if (selLong) { state_ = State::Play; break; }
        if (da) writeTo_ = clampInt(writeTo_ + da, 0, 99);
        if (selShort) {
            const bool ok = mode_ ? writeCombi(writeTo_) : writeProgram(writeTo_);
            popup_ = ok ? "Written" : "Protected";
            popupUntil_ = in.nowMs + kPopupMs;
            state_ = State::Play;
        }
        break;
    case State::Confirm:
        if (selLong || da < 0) { state_ = State::Play; break; }
        if (selShort) {
            if (confirmAction_ == A_RESET && !bridge_.memory().protect) {
                bridge_.memory().factoryReset();
                memoryLoaded();
                popup_ = "Reset done";
            } else {
                popup_ = "Protected";
            }
            popupUntil_ = in.nowMs + kPopupMs;
            state_ = State::Play;
        }
        break;
    }
    return dirty;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
bool M1_Controller::draw(picoface::ui::Display& d, uint32_t nowMs) {
    bool marquee = false;
    switch (state_) {
    case State::Play: marquee = drawPlay(d, nowMs); break;
    case State::Menu: list_.draw(d, mode_ ? "COMBI" : "PROGRAM"); break;
    case State::Send: list_.draw(d, "SEND SYSEX"); break;
    case State::Groups: list_.draw(d, "PAGES"); break;
    case State::Edit: drawEdit(d); break;
    case State::Write: drawWrite(d); break;
    case State::Confirm:
        d.clear();
        kit::popup(d, "FACTORY RESET", "SEL: reset  hold: no");
        break;
    }
    if (popup_ && state_ == State::Play) kit::popup(d, "M1", popup_);
    d.flush();
    return marquee;
}

static void trimName(char* out, const uint8_t* src) {
    memcpy(out, src, 10);
    out[10] = 0;
    for (int i = 0; i < 10; ++i) if (out[i] < 32 || out[i] > 126) out[i] = ' ';
    for (int i = 9; i >= 0 && out[i] == ' '; --i) out[i] = 0;
}

bool M1_Controller::drawPlay(picoface::ui::Display& d, uint32_t nowMs) {
    static const char* const kNames[] = {"", "VOL", "VOICES", "TUNE", "FX", "SYS", "DIAG"};
    char ta[24] = {0}, tb[24] = {0};
    const char* title = page_ == M1Page::PATCH ? (mode_ ? "COMBI" : "PROG") : kNames[(int)page_];
    d.clear();
    kit::header(d, title, (int)page_, (int)M1Page::COUNT);
    switch (page_) {
    case M1Page::PATCH: {
        char nm[11];
        trimName(nm, mode_ ? combiEdit_ : progEdit_);
        snprintf(tb, sizeof tb, "%s%02u%s", mode_ ? "C" : "P", (unsigned)(mode_ ? combi_ : program_),
                 comparing_ ? " CMP" : (edited_ ? " *" : ""));
        return kit::panelName(d, "", nm, tb, {}, nowMs);
    }
    case M1Page::VOLUME: snprintf(ta, sizeof ta, "%u%%", volume_); kit::panelDuo(d, {"Volume", ta, volume_ / 100.0f}, {"", "", -1}); break;
    case M1Page::VOICES:
        snprintf(ta, sizeof ta, "%u", voices_); snprintf(tb, sizeof tb, "%d", bridge_.activeVoices());
        kit::panelDuo(d, {"Max", ta, voices_ / 16.0f}, {"Act", tb, -1});
        break;
    case M1Page::TUNE:
        snprintf(ta, sizeof ta, "%+dct", tune_);
        snprintf(tb, sizeof tb, "%.1fHz", 440.0 * pow(2.0, (tune_ + (int8_t)bridge_.memory().image().global[0]) / 1200.0));
        kit::panelDuo(d, {"Tune", ta, (tune_ + 50) / 100.0f}, {"A4", tb, -1});
        break;
    case M1Page::FX: kit::panelDuo(d, {"Effects", fxOn_ ? "On" : "Bypass", -1}, {"", "", -1}); break;
    case M1Page::SYS:
        if (midiCh_ >= 16) snprintf(ta, sizeof ta, "Omni"); else snprintf(ta, sizeof ta, "Ch %u", midiCh_ + 1u);
        snprintf(tb, sizeof tb, "%lu", sysex_ ? (unsigned long)sysex_->received() : 0ul);
        kit::panelDuo(d, {"MIDI", ta, -1}, {"SysEx", tb, -1});
        break;
    case M1Page::DIAG: {
        char r0[22], r1[22], r2[22];
        snprintf(r0, sizeof r0, "CPU peak %d%%", (int)bridge_.cpuLoadPeakPercent());
        snprintf(r1, sizeof r1, "Voices %d/%d", bridge_.activeVoices(), bridge_.voiceLimit());
        snprintf(r2, sizeof r2, "Mem %s%s", bridge_.memory().loadedFromFlash() ? "user" : "factory",
                 bridge_.memory().dirty() ? " *" : "");
        const char* rows[3] = {r0, r1, r2};
        kit::diagnostics(d, "DIAG", rows, 3);
        break;
    }
    default: break;
    }
    return false;
}

void M1_Controller::drawEdit(picoface::ui::Display& d) {
    d.clear();
    const int g = cursor_ < nParams_ ? paramGroup_[cursor_] : 0;
    // Page dots only while they stay countable; the group list (SEL press)
    // is the map for the long edit sets.
    kit::header(d, nGroups_ ? groupNames_[g] : "EDIT", g, nGroups_ <= 8 ? nGroups_ : 0);
    char ta[16] = {0}, tb[16] = {0};
    float na = -1, nb = -1;
    picoface::ui::kit::Param a{"", "", -1}, b{"", "", -1};
    const uint8_t* blk = editBlock();
    for (int k = 0; k < 2; ++k) {
        const int i = cursor_ + k;
        if (i >= nParams_ || !params_[i].name) break;
        char* t = k ? tb : ta;
        float* nrm = k ? &nb : &na;
        if (target_ == Target::Kit && i < 2) {
            if (i == 0) snprintf(t, 16, "%d", kit_ + 1);
            else snprintf(t, 16, "%d", kitInst_ + 1);
            *nrm = -1;
        } else {
            m1::paramFormat(params_[i], comparing_ && target_ == Target::Program ? bridge_.memory().program(program_) : blk,
                            t, 16, nrm);
        }
        picoface::ui::kit::Param& p = k ? b : a;
        p.name = params_[i].name;
        p.text = t;
        p.norm = *nrm;
    }
    kit::panelDuo(d, a, b);
}

void M1_Controller::drawWrite(picoface::ui::Display& d) {
    d.clear();
    kit::header(d, mode_ ? "WRITE COMBI" : "WRITE PROG", 0, 0);
    char nm[11], num[8];
    trimName(nm, mode_ ? bridge_.memory().combi(writeTo_) : bridge_.memory().program(writeTo_));
    snprintf(num, sizeof num, "%s%02d", mode_ ? "C" : "P", writeTo_);
    kit::panelDuo(d, {"To", num, writeTo_ / 99.0f}, {"Replaces", nm, -1});
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void M1_Controller::exportSettings(M1SettingsV1& s) const {
    s.mode = mode_; s.program = program_; s.combi = combi_;
    s.volume = volume_; s.voices = voices_; s.midiCh = midiCh_;
    s.masterTune = tune_; s.fxOn = fxOn_;
}

void M1_Controller::importSettings(const M1SettingsV1& s) {
    mode_ = s.mode ? 1 : 0;
    program_ = (uint8_t)clampInt(s.program, 0, 99);
    combi_ = (uint8_t)clampInt(s.combi, 0, 99);
    volume_ = (uint8_t)clampInt(s.volume, 0, 100);
    voices_ = (uint8_t)clampInt(s.voices, 1, m1::kVoices);
    midiCh_ = (uint8_t)clampInt(s.midiCh, 0, 16);
    tune_ = (int8_t)clampInt(s.masterTune, -50, 50);
    fxOn_ = s.fxOn ? 1 : 0;
    bridge_.setVolume(volume_);
    bridge_.setVoiceLimit(voices_);
    bridge_.setFxOn(fxOn_ != 0);
    applyGlobal();
    if (mode_) selectCombi(combi_); else selectProgram(program_);
}
