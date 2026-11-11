// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_params.cpp -- the parameter list of a Prophet VS program. Field numbers
// and ranges follow provs/preset_map.h; the names follow the VS's panel.

#include "vs_params.h"

#include <cstdio>

#include "provs/gen/rom_tables.h"   // kNameChars

namespace vs {
namespace {

const char* const kLfoShape[] = {"Tri", "Square", "Saw", "Ramp", "Random"};
const char* const kLoop[] = {"Off", "0>3", "1>3", "2>3", "0<>3", "1<>3", "2<>3"};
const char* const kKeyMode[] = {"Normal", "Split", "Double"};
const char* const kArpMode[] = {"Normal", "AutoLat", "Extend"};
const char* const kArpScan[] = {"Up", "Down", "Up/Down", "Assign", "Reverse", "Random"};
const char* const kArpSplit[] = {"Off", "Right", "Left", "Dual"};
const char* const kVoicing[] = {"Mono", "Poly"};
const char* const kOffOn[] = {"Off", "On"};

#define NUM(name, field, lo, hi)   {name, field, Kind::Num, lo, hi, nullptr, 0}
#define S7(name, field)            {name, field, Kind::Signed7, -64, 63, nullptr, 0}
#define S8(name, field)            {name, field, Kind::Signed8, -99, 99, nullptr, 0}
#define ENUM(name, field, names)   {name, field, Kind::Enum, 0, \
                                    (int16_t)(sizeof(names) / sizeof(names[0]) - 1), names, 0}
#define BIT(name, field, bitNo)    {name, field, Kind::Bit, 0, 1, nullptr, (uint8_t)(1u << (bitNo))}
#define WAVE(name, field)          {name, field, Kind::Wave, 0, 127, nullptr, 0}
#define BLANK                      {nullptr, 0, Kind::Blank, 0, 0, nullptr, 0}

// Five rates and the loop of an envelope starting at field 'r' (rates) with
// loop/repeat at 'l'.
#define RATES(r) NUM("Rate 1", r, 0, 99), NUM("Rate 2", (r) + 1, 0, 99), NUM("Rate 3", (r) + 2, 0, 99), \
                 NUM("Rate 4", (r) + 3, 0, 99), NUM("Rate 4A", (r) + 4, 0, 99)
#define LOOP(l)  ENUM("Loop", l, kLoop), NUM("Repeat", (l) + 1, 0, 7)

// The ten destination switches of an LFO: two bits in field 'hi', eight in
// the field after it.
#define LFO_DESTS(hi, other) \
    BIT("Freq A", (hi) + 1, 0), BIT("Freq B", (hi) + 1, 1), BIT("Freq C", (hi) + 1, 2), \
    BIT("Freq D", (hi) + 1, 3), BIT("Filter", (hi) + 1, 4), BIT("Mix A-C", (hi) + 1, 5), \
    BIT("Mix B-D", (hi) + 1, 6), BIT(other " Rate", (hi) + 1, 7), BIT(other " Amt", hi, 0), \
    BIT("Pan", hi, 1)

struct Group { const char* name; int first; };

const ParamDef kParams[] = {
    // OSC
    WAVE("Wave A", 0), WAVE("Wave B", 1), WAVE("Wave C", 2), WAVE("Wave D", 3),
    NUM("Coarse A", 4, 0, 24), NUM("Fine A", 8, 0, 99),
    NUM("Coarse B", 5, 0, 24), NUM("Fine B", 9, 0, 99),
    NUM("Coarse C", 6, 0, 24), NUM("Fine C", 10, 0, 99),
    NUM("Coarse D", 7, 0, 24), NUM("Fine D", 11, 0, 99),
    // FILTER
    NUM("Cutoff", 12, 0, 99), NUM("Reso", 13, 0, 99), NUM("Env Amt", 14, 0, 99), BLANK,
    // FILTER ENV
    RATES(30), BLANK,
    NUM("Level 0", 35, 0, 99), NUM("Level 1", 36, 0, 99), NUM("Level 2", 37, 0, 99),
    NUM("Level 3", 38, 0, 99), NUM("Level 4", 39, 0, 99), BLANK,
    LOOP(40),
    // AMP ENV
    RATES(19), NUM("VCA Lvl", 69, 0, 99),
    NUM("Level 0", 24, 0, 99), NUM("Level 1", 25, 0, 99), NUM("Level 2", 26, 0, 99),
    NUM("Level 3", 27, 0, 99),
    LOOP(28),
    // MIX ENV
    RATES(42), BLANK,
    S7("X 0", 47), S7("Y 0", 48), S7("X 1", 49), S7("Y 1", 50), S7("X 2", 51), S7("Y 2", 52),
    S7("X 3", 53), S7("Y 3", 54), S7("X 4", 55), S7("Y 4", 56),
    LOOP(57),
    // LFO 1
    ENUM("Shape", 15, kLfoShape), NUM("Rate", 17, 0, 99), NUM("Amount", 96, 0, 99), BLANK,
    LFO_DESTS(102, "LFO2"),
    // LFO 2
    ENUM("Shape", 16, kLfoShape), NUM("Rate", 18, 0, 99), NUM("Amount", 97, 0, 99), BLANK,
    LFO_DESTS(104, "LFO1"),
    // PRESSURE: seven bits in field 106, eight in field 107
    S8("Amount", 98), BLANK,
    BIT("Freq A", 107, 0), BIT("Freq B", 107, 1), BIT("Freq C", 107, 2), BIT("Freq D", 107, 3),
    BIT("Filter", 107, 4), BIT("Mix A-C", 107, 5), BIT("Mix B-D", 107, 6), BIT("LFO1 Rate", 107, 7),
    BIT("LFO1 Amt", 106, 0), BIT("LFO2 Rate", 106, 1), BIT("LFO2 Amt", 106, 2), BIT("Volume", 106, 3),
    BIT("Pan", 106, 4), BIT("Cho Rate", 106, 5), BIT("Cho Depth", 106, 6), BLANK,
    // VELOCITY
    S8("Amount", 99), BLANK,
    BIT("Filt Env", 109, 0), BIT("Mix A-C", 109, 1), BIT("Mix B-D", 109, 2), BIT("Amp", 109, 3),
    BIT("Pan", 109, 4), BLANK,
    // KEYBOARD
    S8("Amount", 100), BLANK,
    BIT("Filter", 111, 0), BIT("Mix A-C", 111, 1), BIT("Mix B-D", 111, 2), BIT("Pan", 111, 3),
    // FILTER ENV MOD
    S8("Amount", 101), BLANK, BIT("Freq", 113, 0), BIT("Pan", 113, 1),
    // MOD WHEEL
    BIT("LFO1 Amt", 115, 0), BIT("LFO2 Amt", 115, 1), BIT("Chorus", 115, 2), BLANK,
    // CHORUS
    BIT("Left", 66, 1), BIT("Right", 66, 0), NUM("Rate", 67, 0, 99), NUM("Depth", 68, 0, 99),
    // VOICE
    NUM("Unison", 64, 0, 7), NUM("Glide", 65, 0, 99),
    ENUM("Key Mode", 59, kKeyMode), {"Split Pt", 60, Kind::Note, 0, 127, nullptr, 0},
    NUM("Link Prg", 61, 0, 99), NUM("Detune", 62, 0, 31),
    {"Delay", 63, Kind::Ms4, 0, 127, nullptr, 0}, BLANK,
    // PAN
    S7("Voice 1", 70), S7("Voice 2", 71), S7("Voice 3", 72), S7("Voice 4", 73),
    S7("Voice 5", 74), S7("Voice 6", 75), S7("Voice 7", 76), S7("Voice 8", 77),
    // ARP
    NUM("Rate", 86, 0, 99), ENUM("Mode", 87, kArpMode),
    ENUM("Scan", 88, kArpScan), NUM("Octaves", 89, 0, 3),
    NUM("Repeats", 90, 0, 3), ENUM("Split", 91, kArpSplit),
    ENUM("Voicing", 92, kVoicing), ENUM("Velocity", 93, kOffOn),
    ENUM("Rests", 95, kOffOn), BLANK,
    // NAME
    {"Char 1", 78, Kind::Char, 0, 31, nullptr, 0}, {"Char 2", 79, Kind::Char, 0, 31, nullptr, 0},
    {"Char 3", 80, Kind::Char, 0, 31, nullptr, 0}, {"Char 4", 81, Kind::Char, 0, 31, nullptr, 0},
    {"Char 5", 82, Kind::Char, 0, 31, nullptr, 0}, {"Char 6", 83, Kind::Char, 0, 31, nullptr, 0},
    {"Char 7", 84, Kind::Char, 0, 31, nullptr, 0}, {"Char 8", 85, Kind::Char, 0, 31, nullptr, 0},
};
constexpr int kParamCount = (int)(sizeof(kParams) / sizeof(kParams[0]));

// Group sizes, in the order of the table above.
constexpr int kGroupSize[] = {12, 4, 14, 12, 18, 14, 14, 18, 8, 6, 4, 4, 4, 8, 8, 10, 8};
const char* const kGroupName[] = {
    "OSC", "FILTER", "FILTER ENV", "AMP ENV", "MIX ENV", "LFO 1", "LFO 2", "PRESSURE",
    "VELOCITY", "KEYBOARD", "FILT ENV MOD", "MOD WHEEL", "CHORUS", "VOICE", "PAN", "ARP", "NAME"};
constexpr int kGroupCount = (int)(sizeof(kGroupSize) / sizeof(kGroupSize[0]));
static_assert(sizeof(kGroupName) / sizeof(kGroupName[0]) == (size_t)kGroupCount, "group names");

constexpr int groupSum(int n) { return n == 0 ? 0 : kGroupSize[n - 1] + groupSum(n - 1); }
static_assert(groupSum(kGroupCount) == kParamCount, "group sizes cover the table");

int value(const ParamDef& p, const uint8_t* f) {
    const int v = f[p.field];
    switch (p.kind) {
    case Kind::Signed7: return v > 63 ? v - 128 : v;
    case Kind::Signed8: return v > 127 ? v - 256 : v;
    case Kind::Bit:     return (v & p.bit) ? 1 : 0;
    default:            return v;
    }
}

const char* const kNoteName[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace

int paramCount() { return kParamCount; }
const ParamDef& param(int index) {
    return kParams[index < 0 ? 0 : (index >= kParamCount ? kParamCount - 1 : index)];
}

int groupCount() { return kGroupCount; }
const char* groupName(int group) {
    return group >= 0 && group < kGroupCount ? kGroupName[group] : "";
}
const char* const* groupNames() { return kGroupName; }
int groupFirst(int group) {
    int first = 0;
    for (int g = 0; g < group && g < kGroupCount; ++g) first += kGroupSize[g];
    return first;
}
int groupOf(int index) {
    int first = 0;
    for (int g = 0; g < kGroupCount; ++g) {
        first += kGroupSize[g];
        if (index < first) return g;
    }
    return kGroupCount - 1;
}

bool paramStep(const ParamDef& p, uint8_t* f, int delta) {
    if (p.kind == Kind::Blank || delta == 0) return false;
    const int old = value(p, f);
    int v = old + delta;
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    if (v == old) return false;
    switch (p.kind) {
    case Kind::Signed7: f[p.field] = (uint8_t)(v & 0x7F); break;
    case Kind::Signed8: f[p.field] = (uint8_t)(v & 0xFF); break;
    case Kind::Bit:     f[p.field] = (uint8_t)(v ? f[p.field] | p.bit : f[p.field] & ~p.bit); break;
    default:            f[p.field] = (uint8_t)v; break;
    }
    return true;
}

void paramFormat(const ParamDef& p, const uint8_t* f, char* text, size_t size, float* norm) {
    float n = -1.0f;
    if (size) text[0] = 0;
    if (p.kind != Kind::Blank) {
        const int v = value(p, f);
        const float pos = p.max > p.min ? (float)(v - p.min) / (float)(p.max - p.min) : 0.0f;
        switch (p.kind) {
        case Kind::Num:
            snprintf(text, size, "%d", v);
            n = pos;
            break;
        case Kind::Signed7:
        case Kind::Signed8:
            snprintf(text, size, "%+d", v);
            n = pos;
            break;
        case Kind::Enum:
            snprintf(text, size, "%s", v >= p.min && v <= p.max ? p.names[v] : "?");
            break;
        case Kind::Bit:
            snprintf(text, size, "%s", v ? "On" : "Off");
            break;
        case Kind::Wave:
            if (v == 127) snprintf(text, size, "Noise");
            else if (v == 126) snprintf(text, size, "Silence");
            else snprintf(text, size, "%d", v);
            n = pos;
            break;
        case Kind::Note:
            snprintf(text, size, "%s%d", kNoteName[v % 12], v / 12 - 1);
            n = pos;
            break;
        case Kind::Char: {
            const char c = provs::kNameChars[v & 31];
            if (c == ' ') snprintf(text, size, "space");
            else snprintf(text, size, "%c", c);
            break;
        }
        case Kind::Ms4:
            snprintf(text, size, "%d ms", v * 4);
            n = pos;
            break;
        default:
            break;
        }
    }
    if (norm) *norm = n;
}

} // namespace vs
