// Delay-line geometry of the JV-880 effect chip, by address trace.
//
// PicoFaceJV's reverb is matched, not reproduced: the decay time, the
// decorrelation and the level are measured, but the network behind them is a
// Schroeder bank of my own, and its tails run about 13 dB low on material that
// decays. This tool answers half of that -- what the chip's delay network
// actually is.
//
// The effects live inside the PCM chip, whose delay memory the reference
// emulator models as `eram`, 0x4000 words, 512 ms at 32 kHz. Every effect slot
// addresses it as
//
//     eram[ (base + tv_counter) & 0x3fff ]
//
// with tv_counter a free-running pointer. Logging every access and taking
// (addr - tv_counter) & 0x3fff therefore recovers the base the firmware
// programmed -- the tap position -- without having to guess.
//
// This is the route munt took for the MT-32, where the buffer sizes came from
// tracing the reverb RAM address lines of the real chip. The MT-32's constants
// are of no use here (different chip, five years earlier) but the method is,
// and in an emulator the address lines are function arguments.
//
// Reading the registers directly does NOT work, and the first version of this
// tool was thrown away for it: the chip walks 32 slots in rotation and reuses
// ram2[28..30] per slot, so a snapshot catches one arbitrary moment and reports
// the same values for every reverb type.
//
// WHAT THIS TOOL CANNOT DO. The coefficients are out of reach, and so is the
// RT60 law -- see README.md, "The reverb, and where it stops". The emulator's
// reverb does not respond to the type or the time setting at all; the geometry
// below is the part that is real.
//
// Build and run: tools/jv_extract/build_fx_taps.sh <romdir>

#include "mcu.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static const int SR = 32000, PATCH_SZ = 0x16A, NV_PATCH = 0x0D70;

// Patch-common bytes, the offsets Engine::Reverb::configure() reads.
static const int OFF_REV_TYPE = 12, OFF_REV_LEVEL = 13, OFF_REV_TIME = 14;
// The send is per tone (jv_tone_map.h, +82); the patch byte is only the return.
static const int TONE_OFF = 26, TONE_SZ = 84, TONE_REV_SEND = 82;

// ------------------------------------------------------------------- the hook
// Called from the patched copy of the emulator's pcm.cpp that build_fx_taps.sh
// generates. Nothing of the emulator is checked in; see README.md, "Credit".

struct Access { int rw, addr, tvc; };
static std::vector<Access> g_log;
static bool g_logging = false;

void jv_fx_log(int rw, int addr, int tvc) {
    if (g_logging) g_log.push_back({rw, addr, tvc});
}

// ------------------------------------------------------------------ ROM load
// Self-contained, as in jv_probe.cpp, so the only external dependency is the
// emulator core.

static bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t want) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    out.resize(want);
    size_t got = fread(out.data(), 1, want, f);
    fclose(f);
    if (got != want) { fprintf(stderr, "%s: short read\n", path.c_str()); return false; }
    return true;
}

static void descramble(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    static const int A[20] = {2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19};
    static const int D[8]  = {2, 0, 4, 5, 7, 6, 3, 1};
    dst.resize(src.size());
    for (size_t page = 0; page < src.size(); page += 0x100000) {
        const size_t n = std::min<size_t>(0x100000, src.size() - page);
        for (size_t i = 0; i < n; i++) {
            size_t addr = 0;
            for (int b = 0; b < 20; b++) if (i & (1u << A[b])) addr |= 1u << b;
            uint8_t data = 0, s = src[page + addr];
            for (int b = 0; b < 8; b++) if (s & (1u << D[b])) data |= 1u << b;
            dst[page + i] = data;
        }
    }
}

static void render(MCU& m, float* l, float* r, int n) {
    const int CH = 4096;   // the emulator's internal render buffer caps one call
    for (int i = 0; i < n; i += CH)
        m.updateSC55WithSampleRate(l + i, r + i, std::min(CH, n - i), SR);
}

// ---------------------------------------------------------------------- main

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: jv_fx_taps <romdir> [patchIndexInBankA] > taps.csv\n");
        return 1;
    }
    const std::string dir = argv[1];
    const int basePatch = argc > 2 ? atoi(argv[2]) : 0;

    std::vector<uint8_t> rom1, rom2, nvram, w1raw, w2raw, w1, w2;
    if (!readFile(dir + "/jv880_rom1.bin", rom1, 0x8000) ||
        !readFile(dir + "/jv880_rom2.bin", rom2, 0x40000) ||
        !readFile(dir + "/jv880_nvram.bin", nvram, 0x8000) ||
        !readFile(dir + "/jv880_waverom1.bin", w1raw, 0x200000) ||
        !readFile(dir + "/jv880_waverom2.bin", w2raw, 0x200000)) return 1;
    descramble(w1raw, w1);
    descramble(w2raw, w2);

    uint8_t base[PATCH_SZ];
    memcpy(base, rom2.data() + 0x010CE0 + basePatch * PATCH_SZ, PATCH_SZ);   // bank A

    const int nWarm = SR, nHold = SR / 2;
    std::vector<float> L(nWarm + nHold), R(nWarm + nHold), S(64), S2(64);

    printf("type,time,reads,writes,taps\n");

    static const int kTimes[] = {0, 64, 127};
    for (int type = 0; type < 8; type++) {
        for (int time : kTimes) {
            // A fresh MCU per point, for the reason jv_probe gives: SC55_Reset()
            // leaves PCM and timer state behind and results become
            // order-dependent.
            MCU* mcu = new MCU();
            mcu->startSC55(rom1.data(), rom2.data(), w1.data(), w2.data(), nvram.data());

            uint8_t patch[PATCH_SZ];
            memcpy(patch, base, PATCH_SZ);
            patch[OFF_REV_TYPE]  = (uint8_t) type;
            patch[OFF_REV_LEVEL] = 127;
            patch[OFF_REV_TIME]  = (uint8_t) time;
            for (int t = 0; t < 4; t++)
                patch[TONE_OFF + t * TONE_SZ + TONE_REV_SEND] = 127;
            memcpy(mcu->nvram + NV_PATCH, patch, PATCH_SZ);
            mcu->nvram[0x11] = 1;             // patch mode, not rhythm
            mcu->SC55_Reset();

            render(*mcu, L.data(), R.data(), nWarm);
            uint8_t on[3] = {0x90, 60, 100};
            mcu->postMidiSC55(on, 3);
            render(*mcu, L.data() + nWarm, R.data() + nWarm, nHold);

            // 64 sample periods is many full slot cycles, so every tap the
            // configuration uses is hit at least once.
            g_log.clear();
            g_logging = true;
            render(*mcu, S.data(), S2.data(), 64);
            g_logging = false;

            std::map<int, int> reads, writes;
            for (const Access& a : g_log)
                (a.rw ? writes : reads)[(a.addr - a.tvc) & 0x3fff]++;

            printf("%d,%d,%zu,%zu,\"", type, time, reads.size(), writes.size());
            bool first = true;
            for (const auto& kv : reads) {
                printf("%s%d", first ? "" : " ", kv.first);
                first = false;
            }
            printf("\"\n");
            fflush(stdout);
            delete mcu;
        }
    }
    return 0;
}
