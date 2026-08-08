// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// How much of PicoFaceRD's render time is waiting for flash?
//
// The engine reads one 32-bit wave-ROM word per part per sample
// (`_bank[waverom_addr]`), and with ten parts per note and twelve voices that
// is up to 120 loads per sample, each of which may miss the XIP cache. The
// architecture notes call that miss stream the dominant cost. This counts it.
//
// The address stream is captured from the real engine (a compile-time hook in
// rd_new_engine.cpp calls rd_xip_trace at exactly the access site) and run
// through a model of the RP2350's XIP cache. The geometry is the one the engine's
// own comment states -- "two entries per XIP line", so 8-byte lines -- at the
// RP2350's 16 KB with two-way associativity.
//
// The cycle figure is an estimate and is labelled as one: it multiplies misses
// by a QSPI fetch cost, and the true cost depends on bus arbitration this model
// does not have. The miss RATE is the measurement; the stall fraction is what
// follows from it under stated assumptions.

#include "rd_new_engine.h"
#include "rd_packs_data.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ------------------------------------------------------------- address trace
static std::vector<unsigned> g_addrs;
static bool g_logging = false;

void rd_xip_trace(unsigned addr) {
    if (g_logging) g_addrs.push_back(addr);
}

// ------------------------------------------------------------- cache model
struct Cache {
    // RP2350: 16 KB XIP cache. Line = 8 bytes (two 4-byte wave entries), which
    // is what the engine's packing comment assumes. Two-way set associative.
    static const int kLineBytes = 8;
    static const int kWays = 2;
    static const int kSets = (16 * 1024) / (kLineBytes * kWays);

    unsigned tag[kSets][kWays];
    bool     valid[kSets][kWays];
    int      lru[kSets];
    long     hits = 0, misses = 0;

    Cache() { memset(valid, 0, sizeof(valid)); memset(lru, 0, sizeof(lru)); }

    void access(unsigned byteAddr) {
        const unsigned line = byteAddr / kLineBytes;
        const int set = (int)(line % kSets);
        const unsigned t = line / kSets;
        for (int w = 0; w < kWays; w++) {
            if (valid[set][w] && tag[set][w] == t) { hits++; lru[set] = 1 - w; return; }
        }
        misses++;
        const int victim = lru[set];
        tag[set][victim] = t;
        valid[set][victim] = true;
        lru[set] = 1 - victim;
    }
};

int main(int argc, char** argv) {
    const int patch  = argc > 1 ? atoi(argv[1]) : 0;
    const int voices = argc > 2 ? atoi(argv[2]) : 12;
    const int blocks = argc > 3 ? atoi(argv[3]) : 200;

    RdNewEngine eng;
    // The bank is carried inside the pack header, so loadPack takes only the blob.
    if (!eng.loadPack(rd_pack_ptrs[patch], rd_pack_sizes[patch])) {
        fprintf(stderr, "pack %d did not load\n", patch);
        return 1;
    }
    eng.setVoiceLimit((uint8_t) voices);

    // A held chord across the keyboard, so every voice runs a different note
    // and the parts do not share wave regions by accident.
    for (int i = 0; i < voices; i++) eng.noteOn((uint8_t)(36 + i * 3), 100);

    int32_t acc[64];
    for (int b = 0; b < 8; b++) { memset(acc, 0, sizeof(acc)); eng.renderBlock(acc, 64); }   // settle

    g_logging = true;
    for (int b = 0; b < blocks; b++) { memset(acc, 0, sizeof(acc)); eng.renderBlock(acc, 64); }
    g_logging = false;

    const long samples = (long) blocks * 64;
    Cache c;
    for (unsigned a : g_addrs) c.access(a * 4u);   // entries are 4 bytes

    const double perSample = (double) g_addrs.size() / (double) samples;
    const double missRate  = c.misses ? (double) c.misses / (double)(c.hits + c.misses) : 0.0;

    printf("patch %d, %d voices, %ld samples\n", patch, voices, samples);
    printf("  wave-ROM loads      %10zu   (%.1f per sample)\n", g_addrs.size(), perSample);
    printf("  XIP hits            %10ld\n", c.hits);
    printf("  XIP misses          %10ld   (%.1f %% miss rate)\n", c.misses, 100.0 * missRate);
    printf("  misses per sample   %10.1f\n", (double) c.misses / (double) samples);

    // Budget: at 32 kHz one sample is 480e6/32000 = 15000 CPU cycles across two
    // cores. A miss fetches one line over QSPI at 120 MHz; a quad read costs
    // roughly 8+8 QSPI clocks of overhead plus the data, call it 24, which at
    // the 4:1 clock ratio is ~96 CPU cycles. Both numbers are stated so the
    // estimate can be argued with.
    const double cyclesPerSample = 480e6 / 32000.0;
    const double missCycles = (double) c.misses / (double) samples * 96.0;
    printf("\n  cycle budget/sample %10.0f  (480 MHz, 32 kHz, both cores)\n", cyclesPerSample);
    printf("  estimated stall     %10.0f  cycles/sample at ~96 per miss\n", missCycles);
    printf("  -> flash-bound      %10.1f %% of the budget\n", 100.0 * missCycles / cyclesPerSample);
    return 0;
}
