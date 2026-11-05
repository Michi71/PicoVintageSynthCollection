// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_params.cpp -- the parameter tables and their formatting.
#include "m1_params.h"

#include <stdio.h>
#include <string.h>

namespace m1 {

namespace {
const char* const* g_msNames = nullptr;
const char* const* g_drumNames = nullptr;
const uint8_t* g_programs = nullptr;

#define P(name, off, kind, lo, hi, aux) { name, off, PK::kind, lo, hi, aux }

// ------------------------------------------------------------- program
const ParamDef kName[] = {
    P("Char 1", 0, Char, 32, 126, 0), P("Char 2", 1, Char, 32, 126, 0),
    P("Char 3", 2, Char, 32, 126, 0), P("Char 4", 3, Char, 32, 126, 0),
    P("Char 5", 4, Char, 32, 126, 0), P("Char 6", 5, Char, 32, 126, 0),
    P("Char 7", 6, Char, 32, 126, 0), P("Char 8", 7, Char, 32, 126, 0),
    P("Char 9", 8, Char, 32, 126, 0), P("Char 10", 9, Char, 32, 126, 0),
};
const ParamDef kOscBasic[] = {
    P("Osc Mode", 10, OscMode, 0, 2, 0), P("Assign", 11, Bit, 0, 1, 0), P("Hold", 11, Bit, 0, 1, 1),
};
const ParamDef kOsc1[] = {
    P("Multisnd", 12, Multi, 0, 99, 0), P("Octave", 13, Octave, -1, 1, 0), P("Level", 86, U, 0, 99, 0),
};
const ParamDef kOsc2[] = {
    P("Multisnd", 14, Multi, 0, 99, 0), P("Octave", 15, Octave, -1, 1, 0), P("Level", 126, U, 0, 99, 0),
    P("Interval", 16, S, -12, 12, 0), P("Detune", 17, S, -50, 50, 0), P("Delay", 18, U, 0, 99, 0),
};
#define PEG(b) { \
    P("Start Lvl", b + 0, S, -99, 99, 0), P("Atk Time", b + 1, U, 0, 99, 0), \
    P("Atk Level", b + 2, S, -99, 99, 0), P("Dcy Time", b + 3, U, 0, 99, 0), \
    P("Rel Time", b + 4, U, 0, 99, 0), P("Rel Level", b + 5, S, -99, 99, 0), \
    P("Time Vel", b + 6, S, -99, 99, 0), P("Level Vel", b + 7, S, -99, 99, 0) }
const ParamDef kPeg1[] = PEG(63);
const ParamDef kPeg2[] = PEG(103);
#define VDF(b) { \
    P("Cutoff", b + 8, U, 0, 99, 0), P("EG Int", b + 11, U, 0, 99, 0), \
    P("Kbd Center", b + 9, Note, 0, 127, 0), P("Kbd Track", b + 10, S, -99, 99, 0), \
    P("EG Int Vel", b + 14, S, -99, 99, 0), P("EG Tm Kbd", b + 12, U, 0, 99, 0), \
    P("EG Tm Vel", b + 13, U, 0, 99, 0) }
const ParamDef kVdf1[] = VDF(63);
const ParamDef kVdf2[] = VDF(103);
#define VDFEG(b) { \
    P("Atk Time", b + 15, U, 0, 99, 0), P("Atk Level", b + 16, S, -99, 99, 0), \
    P("Dcy Time", b + 17, U, 0, 99, 0), P("Break Pt", b + 18, S, -99, 99, 0), \
    P("Slope Tm", b + 19, U, 0, 99, 0), P("Sustain", b + 20, S, -99, 99, 0), \
    P("Rel Time", b + 21, U, 0, 99, 0), P("Rel Level", b + 22, S, -99, 99, 0) }
const ParamDef kVdfEg1[] = VDFEG(63);
const ParamDef kVdfEg2[] = VDFEG(103);
#define SW(b, kb, vb) { \
    P("Kbd Atk", b + kb, SwPol, 0, 2, 0), P("Kbd Dcy", b + kb, SwPol, 0, 2, 1), \
    P("Kbd Slope", b + kb, SwPol, 0, 2, 2), P("Kbd Rel", b + kb, SwPol, 0, 2, 3), \
    P("Vel Atk", b + vb, SwPol, 0, 2, 0), P("Vel Dcy", b + vb, SwPol, 0, 2, 1), \
    P("Vel Slope", b + vb, SwPol, 0, 2, 2), P("Vel Rel", b + vb, SwPol, 0, 2, 3) }
const ParamDef kVdfSw1[] = SW(63, 36, 37);
const ParamDef kVdfSw2[] = SW(103, 36, 37);
#define VDA(b) { \
    P("Kbd Center", b + 24, Note, 0, 127, 0), P("Kbd Track", b + 25, S, -99, 99, 0), \
    P("Vel Sense", b + 26, S, -99, 99, 0), P("EG Tm Kbd", b + 27, U, 0, 99, 0), \
    P("EG Tm Vel", b + 28, U, 0, 99, 0) }
const ParamDef kVda1[] = VDA(63);
const ParamDef kVda2[] = VDA(103);
#define VDAEG(b) { \
    P("Atk Time", b + 29, U, 0, 99, 0), P("Atk Level", b + 30, U, 0, 99, 0), \
    P("Dcy Time", b + 31, U, 0, 99, 0), P("Break Pt", b + 32, U, 0, 99, 0), \
    P("Slope Tm", b + 33, U, 0, 99, 0), P("Sustain", b + 34, U, 0, 99, 0), \
    P("Rel Time", b + 35, U, 0, 99, 0) }
const ParamDef kVdaEg1[] = VDAEG(63);
const ParamDef kVdaEg2[] = VDAEG(103);
const ParamDef kVdaSw1[] = SW(63, 38, 39);
const ParamDef kVdaSw2[] = SW(103, 38, 39);
#define MG(b) { \
    P("Wave", b, Wave, 0, 3, 0), P("Frequency", b + 1, U, 0, 99, 0), \
    P("Delay", b + 2, U, 0, 99, 0), P("Intensity", b + 3, U, 0, 99, 0), \
    P("OSC1", b, Bit, 0, 1, 5), P("OSC2", b, Bit, 0, 1, 6), P("Key Sync", b, Bit, 0, 1, 7) }
const ParamDef kPmg[] = MG(19);
const ParamDef kVmg[] = MG(23);
const ParamDef kAt[] = {
    P("Pitch", 27, S, -12, 12, 0), P("Pitch MG", 28, U, 0, 99, 0), P("VDF Cutoff", 29, S, -99, 99, 0),
    P("VDF MG", 30, U, 0, 99, 0), P("VDA Amp", 31, S, -99, 99, 0),
};
const ParamDef kJs[] = {
    P("Bend", 32, S, -12, 12, 0), P("VDF Sweep", 33, S, -99, 99, 0), P("Pitch MG", 34, U, 0, 99, 0),
    P("PMG Freq", 35, U, 0, 3, 0), P("VDF MG", 36, U, 0, 99, 0), P("VMG Freq", 37, U, 0, 3, 0),
};
#define FXSEC(b) { \
    P("FX1 Type", b + 0, FxType, 0, 33, 0), P("FX2 Type", b + 1, FxType, 0, 33, 0), \
    P("Placement", b + 8, Placement, 0, 1, 4), P("FX1 L On", b + 8, Bit, 0, 1, 0), \
    P("FX1 R On", b + 8, Bit, 0, 1, 1), P("FX2 L On", b + 8, Bit, 0, 1, 2), \
    P("FX2 R On", b + 8, Bit, 0, 1, 3), P("FX1 Bal L", b + 2, U, 0, 100, 0), \
    P("FX1 Bal R", b + 3, U, 0, 100, 0), P("FX2 Bal L", b + 4, U, 0, 100, 0), \
    P("FX2 Bal R", b + 5, U, 0, 100, 0), P("Out3 Pan", b + 6, FxPan, 0, 101, 0), \
    P("Out4 Pan", b + 7, FxPan, 0, 101, 0) }
const ParamDef kProgFxSec[] = FXSEC(38);
const ParamDef kCombiFxSec[] = FXSEC(11);

#define N(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
const PageDef kProgramPages[] = {
    {"Name", kName, N(kName), 0},
    {"OSC Basic", kOscBasic, N(kOscBasic), 0},
    {"OSC 1", kOsc1, N(kOsc1), 0},
    {"OSC 2", kOsc2, N(kOsc2), kPageDoubleOnly},
    {"Pitch EG 1", kPeg1, N(kPeg1), 0},
    {"Pitch EG 2", kPeg2, N(kPeg2), kPageDoubleOnly},
    {"VDF 1", kVdf1, N(kVdf1), 0},
    {"VDF 1 EG", kVdfEg1, N(kVdfEg1), 0},
    {"VDF 1 Sw", kVdfSw1, N(kVdfSw1), 0},
    {"VDF 2", kVdf2, N(kVdf2), kPageDoubleOnly},
    {"VDF 2 EG", kVdfEg2, N(kVdfEg2), kPageDoubleOnly},
    {"VDF 2 Sw", kVdfSw2, N(kVdfSw2), kPageDoubleOnly},
    {"VDA 1", kVda1, N(kVda1), 0},
    {"VDA 1 EG", kVdaEg1, N(kVdaEg1), 0},
    {"VDA 1 Sw", kVdaSw1, N(kVdaSw1), 0},
    {"VDA 2", kVda2, N(kVda2), kPageDoubleOnly},
    {"VDA 2 EG", kVdaEg2, N(kVdaEg2), kPageDoubleOnly},
    {"VDA 2 Sw", kVdaSw2, N(kVdaSw2), kPageDoubleOnly},
    {"Pitch MG", kPmg, N(kPmg), 0},
    {"VDF MG", kVmg, N(kVmg), 0},
    {"AfterTouch", kAt, N(kAt), 0},
    {"Joystick", kJs, N(kJs), 0},
    {"Effects", kProgFxSec, N(kProgFxSec), 0},
    {"Effect 1", nullptr, 8, kPageFx1},
    {"Effect 2", nullptr, 8, kPageFx2},
};

// --------------------------------------------------------- combination
const ParamDef kCombiType[] = { P("Type", 10, CombiType, 0, 4, 0) };
const ParamDef kTimbre[] = {
    P("Program", 36, Program, 0, 99, 0), P("Level", 37, U, 0, 99, 0),
    P("Transpose", 38, S, -24, 24, 0), P("Detune", 39, S, -50, 50, 0),
    P("Pan", 40, TimPan, 0, 14, 0), P("Key Top", 41, Note, 0, 127, 0),
    P("Key Bottom", 42, Note, 0, 127, 0), P("Vel Top", 43, U, 1, 127, 0),
    P("Vel Bottom", 44, U, 1, 127, 0), P("MIDI Ch", 46, Field, 1, 16, 0),
    P("Timbre", 46, Toggle, 0, 1, 4), P("Prg Chg", 45, Bit, 0, 1, 0),
    P("Damper", 45, Bit, 0, 1, 1), P("AfterTch", 45, Bit, 0, 1, 2),
    P("Ctrl Chg", 45, Bit, 0, 1, 3),
};
const PageDef kCombiPages[] = {
    {"Name", kName, N(kName), 0},
    {"Combi Type", kCombiType, N(kCombiType), 0},
    {"Timbre", kTimbre, N(kTimbre), kPageTimbre},
    {"Effects", kCombiFxSec, N(kCombiFxSec), 0},
    {"Effect 1", nullptr, 8, kPageFx1},
    {"Effect 2", nullptr, 8, kPageFx2},
};

// -------------------------------------------------------------- global
const ParamDef kGlobal[] = {
    P("Mstr Tune", 0, S, -50, 50, 0), P("Key Transp", 1, S, -12, 12, 0),
    P("Damper Pol", 2, U, 0, 1, 0), P("Pedal 1", 3, U, 0, 9, 0), P("Pedal 2", 4, U, 0, 9, 0),
};
const ParamDef kScale[] = {
    P("Scale", 5, Scale, 0, 4, 0), P("Pure Key", 6, U, 0, 11, 0),
    P("User C", 7, S, -50, 50, 0), P("User C#", 8, S, -50, 50, 0), P("User D", 9, S, -50, 50, 0),
    P("User D#", 10, S, -50, 50, 0), P("User E", 11, S, -50, 50, 0), P("User F", 12, S, -50, 50, 0),
    P("User F#", 13, S, -50, 50, 0), P("User G", 14, S, -50, 50, 0), P("User G#", 15, S, -50, 50, 0),
    P("User A", 16, S, -50, 50, 0), P("User A#", 17, S, -50, 50, 0), P("User B", 18, S, -50, 50, 0),
};
const PageDef kGlobalPages[] = {
    {"Global", kGlobal, N(kGlobal), 0},
    {"Scale", kScale, N(kScale), 0},
};

// --------------------------------------------------- drum kit instrument
const ParamDef kKit[] = {
    P("Sound", 0, DrumSnd, 0, 44, 0), P("Key", 1, Note, 0, 127, 0), P("Pan", 2, Pan, 0, 13, 0),
    P("Tune", 3, Tune10, -120, 120, 0), P("Level", 4, S, -99, 99, 0), P("Decay", 5, S, -99, 99, 0),
};
const PageDef kKitPages[] = { {"Instrument", kKit, N(kKit), 0} };

// -------------------------------------------------------------- effects
// *11 of the MIDI implementation; firmware numbering (0 = Hall). Byte roles
// as the OS's effect compiler reads them (doc/ENGINE.md).
#define E(name, off, kind, lo, hi, aux) P(name, off, kind, lo, hi, aux)
#define X {nullptr, 0, PK::U, 0, 0, 0}
const ParamDef kRev[8] = { E("Rev Time", 0, U, 0, 97, 0), X, E("High Damp", 2, U, 0, 99, 0),
    E("Pre Delay", 3, U, 0, 200, 0), E("E/R Level", 4, U, 0, 99, 0), X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kRoom[8] = { E("Rev Time", 0, U, 0, 47, 0), X, E("High Damp", 2, U, 0, 99, 0),
    E("Pre Delay", 3, U, 0, 200, 0), E("E/R Level", 4, U, 0, 99, 0), X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kEr[8] = { E("E/R Time", 0, U, 0, 70, 0), E("Pre Delay", 1, U, 0, 200, 0), X, X, X, X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kDly[8] = { E("Delay L", 0, Word, 0, 500, 0), X, E("Feedback", 2, S, -99, 99, 0),
    E("High Damp", 3, U, 0, 99, 0), E("Delay R", 4, Word, 0, 500, 0), X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kCho[8] = { E("Depth", 0, U, 0, 99, 0), E("Speed", 1, Hz, 0, 216, 0),
    E("Triangle", 2, Bit, 0, 1, 0), X, E("Delay", 4, U, 0, 200, 0), X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kFlg[8] = { E("Depth", 0, U, 0, 99, 0), E("Speed", 1, Hz, 0, 216, 0),
    E("MG Status", 2, U, 0, 7, 0), E("Feedback", 3, S, -99, 99, 0), E("Delay", 4, U, 0, 50, 0), X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kPha[8] = { E("Depth", 0, U, 0, 99, 0), E("Speed", 1, Hz, 0, 216, 0),
    E("MG Status", 2, U, 0, 3, 0), E("Feedback", 3, S, -99, 99, 0), E("Manual", 4, U, 0, 99, 0), X, X, X };
const ParamDef kTrm[8] = { E("Depth", 0, U, 0, 99, 0), E("Speed", 1, Hz, 0, 216, 0),
    E("MG Status", 2, U, 0, 7, 0), E("Shape", 3, S, -99, 99, 0), X, X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kEq[8] = { X, X, X, X, E("High fc", 4, Freq3, 0, 2, 1), E("Low fc", 5, Freq3, 0, 2, 0),
    E("High Gain", 6, S, -12, 12, 0), E("Low Gain", 7, S, -12, 12, 0) };
const ParamDef kOd[8] = { X, X, E("Drive", 2, U, 0, 99, 0), E("Level", 3, U, 0, 99, 0), X, X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kDst[8] = { X, X, E("Distortion", 2, U, 0, 99, 0), E("Level", 3, U, 0, 99, 0), X, X, X,
    E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kExc[8] = { E("Blend", 0, S, -99, 99, 0), E("Emphasis", 1, U, 0, 9, 0), X, X, X, X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kSym[8] = { E("Depth", 0, U, 0, 99, 0), X, X, X, X, X,
    E("EQ High", 6, S, -12, 12, 0), E("EQ Low", 7, S, -12, 12, 0) };
const ParamDef kRot[8] = { E("Depth", 0, U, 0, 99, 0), X, E("Speed Rate", 2, S, -10, 10, 0), X, X, X, X,
    E("Fast", 7, U, 0, 1, 0) };
#define DLYH E("Delay", 0, Word, 0, 500, 0), X, E("Feedback", 2, S, -99, 99, 0), E("High Damp", 3, U, 0, 99, 0)
const ParamDef kDHall[8] = { DLYH, E("Rev Time", 4, U, 0, 97, 0), X, E("Rev Damp", 6, U, 0, 99, 0),
    E("Pre Delay", 7, U, 0, 150, 0) };
const ParamDef kDRoom[8] = { DLYH, E("Rev Time", 4, U, 0, 47, 0), X, E("Rev Damp", 6, U, 0, 99, 0),
    E("Pre Delay", 7, U, 0, 150, 0) };
const ParamDef kDEr[8] = { DLYH, E("E/R Time", 4, U, 0, 30, 0), E("Pre Delay", 5, U, 0, 100, 0), X, X };
const ParamDef kDDly[8] = { DLYH, E("Delay R", 4, Word, 0, 500, 0), X, E("Feedback R", 6, S, -99, 99, 0),
    E("Damp R", 7, U, 0, 99, 0) };
const ParamDef kDCho[8] = { DLYH, E("Depth", 4, U, 0, 99, 0), E("Speed", 5, Hz, 0, 216, 0),
    E("Triangle", 6, Bit, 0, 1, 0), X };
const ParamDef kDFlg[8] = { DLYH, E("Depth", 4, U, 0, 99, 0), E("Speed", 5, Hz, 0, 216, 0),
    E("Triangle", 6, Bit, 0, 1, 0), E("Feedback", 7, S, -99, 99, 0) };
const ParamDef kDPha[8] = { DLYH, E("Depth", 4, U, 0, 99, 0), E("Speed", 5, Hz, 0, 216, 0),
    E("Feedback", 6, S, -99, 99, 0), E("Manual", 7, U, 0, 99, 0) };
const ParamDef kDTrm[8] = { DLYH, E("Depth", 4, U, 0, 99, 0), E("Speed", 5, Hz, 0, 216, 0), X,
    E("Shape", 7, S, -99, 99, 0) };
const ParamDef kNone[8] = { X, X, X, X, X, X, X, X };

const char* const kOscModeNames[] = {"Single", "Double", "Drums"};
const char* const kWaveNames[] = {"Triangle", "Saw Up", "Saw Down", "Square"};
const char* const kPanNames[] = {"A", "9:1", "8:2", "7:3", "6:4", "5:5", "4:6", "3:7", "2:8", "1:9",
                                 "B", "C", "C+D", "D"};
const char* const kCombiTypeNames[] = {"Single", "Layer", "Split", "Vel.SW", "Multi"};
const char* const kScaleNames[] = {"Equal 1", "Equal 2", "Pure Maj", "Pure Min", "User"};
const char* const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Chorus/flanger speed byte to Hz: the firmware's increment (FX:0671) at
// 31.25 kHz over a 2^20 phase.
float speedHz(int v) {
    int inc;
    if (v <= 99) inc = 3 * (v + 1);
    else if (v <= 199) inc = (v - 99) * 10 + 0x136;
    else inc = (v - 199) * 100 + 0x578;
    inc = inc * 100 / 0x12A;
    return inc * 31250.0f / 1048576.0f;
}

}  // namespace

const char* const kFxNames[34] = {
    "Hall", "Ens Hall", "ConcHall", "Room", "LargeRoom", "LiveStage", "E.Ref 1", "E.Ref 2",
    "E.Ref 3", "St Delay", "CrossDly", "Chorus 1", "Chorus 2", "Flanger 1", "CrossFlng",
    "Phaser 1", "Phaser 2", "Tremolo 1", "Tremolo 2", "Equalizer", "OverDrive", "Distortion",
    "Exciter", "Symph Ens", "Rotary SP", "Dly/Hall", "Dly/Room", "Dly/E.Ref", "Dly/Dly",
    "Dly/Chorus", "Dly/Flngr", "Dly/Phasr", "Dly/Treml", "No Effect",
};

const PageDef* programPages(int& n) { n = N(kProgramPages); return kProgramPages; }
const PageDef* combiPages(int& n)   { n = N(kCombiPages); return kCombiPages; }
const PageDef* globalPages(int& n)  { n = N(kGlobalPages); return kGlobalPages; }
const PageDef* kitPages(int& n)     { n = N(kKitPages); return kKitPages; }

const ParamDef* effectParams(int t) {
    if (t <= 2 || t == 4 || t == 5) return kRev;
    if (t == 3) return kRoom;
    if (t <= 8) return kEr;
    if (t <= 10) return kDly;
    if (t <= 12) return kCho;
    if (t <= 14) return kFlg;
    if (t <= 16) return kPha;
    if (t <= 18) return kTrm;
    if (t == 19) return kEq;
    if (t == 20) return kOd;
    if (t == 21) return kDst;
    if (t == 22) return kExc;
    if (t == 23) return kSym;
    if (t == 24) return kRot;
    switch (t) {
        case 25: return kDHall; case 26: return kDRoom; case 27: return kDEr; case 28: return kDDly;
        case 29: return kDCho;  case 30: return kDFlg;  case 31: return kDPha; case 32: return kDTrm;
        default: return kNone;
    }
}

void setNameSources(const char* const* ms, const char* const* drums, const uint8_t* programs) {
    g_msNames = ms; g_drumNames = drums; g_programs = programs;
}

int paramGet(const ParamDef& p, const uint8_t* b) {
    const uint8_t v = b[p.offset];
    switch (p.kind) {
        case PK::S: case PK::Octave: case PK::Tune10: return (int8_t)v;
        case PK::Bit: case PK::Placement: return (v >> p.aux) & 1;
        case PK::Toggle: return ((v >> p.aux) & 1) ? 0 : 1;          // bit set = off
        case PK::Field: return (v & 0x0F) + 1;                         // MIDI channel 1..16
        case PK::SwPol: return !(v & (1 << p.aux)) ? 0 : ((v & (0x10 << p.aux)) ? 2 : 1);
        case PK::Word: return v | (b[p.offset + 1] << 8);
        case PK::Wave: return v & 3;
        case PK::TimPan: return (v & 0x80) ? 14 : (v & 0x0F);         // 14 = INS
        default: return v;
    }
}

int paramSet(const ParamDef& p, uint8_t* b, int v) {
    if (v < p.lo) v = p.lo;
    if (v > p.hi) v = p.hi;
    uint8_t& x = b[p.offset];
    switch (p.kind) {
        case PK::Bit: case PK::Placement: x = (uint8_t)((x & ~(1 << p.aux)) | (v << p.aux)); break;
        case PK::Toggle: x = (uint8_t)((x & ~(1 << p.aux)) | ((v ? 0 : 1) << p.aux)); break;
        case PK::Field: x = (uint8_t)((x & 0xF0) | (v - 1)); break;
        case PK::SwPol: {
            const uint8_t on = 1 << p.aux, neg = 0x10 << p.aux;
            x = (uint8_t)(x & ~(on | neg));
            if (v >= 1) x |= on;
            if (v == 2) x |= neg;
            break;
        }
        case PK::Word: x = (uint8_t)(v & 0xFF); b[p.offset + 1] = (uint8_t)(v >> 8); break;
        case PK::Wave: x = (uint8_t)((x & ~3) | v); break;
        case PK::TimPan: x = v == 14 ? (uint8_t)((x & 0x0F) | 0x80) : (uint8_t)v; break;
        default: x = (uint8_t)(int8_t)v; break;
    }
    return v;
}

bool paramStep(const ParamDef& p, uint8_t* b, int delta) {
    const int old = paramGet(p, b);
    int step = delta;
    if (p.kind == PK::Word) step *= 5;                   // delay times in 5 ms steps
    return paramSet(p, b, old + step) != old;
}

void paramFormat(const ParamDef& p, const uint8_t* b, char* o, size_t n, float* norm) {
    const int v = paramGet(p, b);
    if (norm) {
        // A knob for values with a position; a list marker for names and choices.
        switch (p.kind) {
            case PK::U: case PK::S: case PK::Word: case PK::Note: case PK::Tune10: case PK::Hz: case PK::FxPan:
                *norm = p.hi > p.lo ? (float)(v - p.lo) / (float)(p.hi - p.lo) : -1.0f;
                break;
            default:
                *norm = -1.0f;
                break;
        }
    }
    switch (p.kind) {
        case PK::S: case PK::Tune10:
            if (p.kind == PK::Tune10) snprintf(o, n, "%+d ct", v * 10);
            else snprintf(o, n, v > 0 ? "+%d" : "%d", v);
            break;
        case PK::Bit: case PK::Toggle:
            if (p.offset == 11 && p.aux == 0) snprintf(o, n, "%s", v ? "Mono" : "Poly");
            else snprintf(o, n, "%s", v ? "On" : "Off");
            break;
        case PK::Placement: snprintf(o, n, "%s", v ? "Serial" : "Parallel"); break;
        case PK::SwPol: snprintf(o, n, "%s", v == 0 ? "Off" : (v == 1 ? "+" : "-")); break;
        case PK::Word: snprintf(o, n, "%d ms", v); break;
        case PK::Char: snprintf(o, n, "'%c'", v >= 32 && v < 127 ? v : ' '); break;
        case PK::Note: snprintf(o, n, "%s%d", kNoteNames[v % 12], v / 12 - 1); break;
        case PK::Multi:
            if (b[10] == 2 && p.offset == 12) snprintf(o, n, "Kit %d", (v & 3) + 1);
            else snprintf(o, n, "%s", g_msNames ? g_msNames[v] : "?");
            break;
        case PK::Octave: snprintf(o, n, "%s", v < 0 ? "16'" : (v > 0 ? "4'" : "8'")); break;
        case PK::OscMode: snprintf(o, n, "%s", kOscModeNames[v % 3]); break;
        case PK::Wave: snprintf(o, n, "%s", kWaveNames[v & 3]); break;
        case PK::Pan: snprintf(o, n, "%s", v < 14 ? kPanNames[v] : "?"); break;
        case PK::TimPan: snprintf(o, n, "%s", v < 14 ? kPanNames[v] : "Inst"); break;
        case PK::FxType: snprintf(o, n, "%s", v < 34 ? kFxNames[v] : "?"); break;
        case PK::FxPan:
            if (v == 0) snprintf(o, n, "Off");
            else if (v == 101) snprintf(o, n, "L");
            else if (v == 1) snprintf(o, n, "R");
            else snprintf(o, n, "%02d:%02d", v - 1, 101 - v);
            break;
        case PK::Program:
            if (g_programs && v < 100) {
                char nm[11];
                memcpy(nm, g_programs + 143 * v, 10);
                nm[10] = 0;
                for (int i = 0; i < 10; ++i) if (nm[i] < 32) nm[i] = ' ';
                snprintf(o, n, "%02d %.7s", v, nm);
            } else snprintf(o, n, "%d", v);
            break;
        case PK::DrumSnd:
            snprintf(o, n, "%s", v == 0 ? "Off" : (g_drumNames ? g_drumNames[v - 1] : "?"));
            break;
        case PK::CombiType: snprintf(o, n, "%s", kCombiTypeNames[v % 5]); break;
        case PK::Scale: snprintf(o, n, "%s", kScaleNames[v % 5]); break;
        case PK::Hz: snprintf(o, n, "%.2f Hz", (double)speedHz(v)); break;
        case PK::Freq3: {
            static const char* const lo[3] = {"250 Hz", "500 Hz", "1 kHz"};
            static const char* const hi[3] = {"1 kHz", "2 kHz", "4 kHz"};
            snprintf(o, n, "%s", (p.aux ? hi : lo)[v % 3]);
            break;
        }
        case PK::Field: snprintf(o, n, "Ch %d", v); break;
        default: snprintf(o, n, "%d", v); break;
    }
}

}  // namespace m1
