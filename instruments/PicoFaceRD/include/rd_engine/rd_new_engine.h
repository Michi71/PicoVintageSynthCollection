// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// The descriptor-replay engine: own code over data the reference emulator
// captured offline. See instruments/PicoFaceRD/README.md.

// rd_new_engine.h -- descriptor-driven SA engine.
// Part math is bit-identical to the chip emulation; the expensive parts
// (6301 interpreter, 64-slot scan, firmware) are replaced by a segment
// scheduler driven by end-of-envelope events, exactly like the chip
// IRQ -> firmware chain it replaces.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "rom_tables.h"

struct RdnSeg {
    uint32_t t;
    uint8_t dest;
    uint8_t speed;
};

struct RdnPartDesc {
    uint8_t flags;
    uint8_t env_offset;
    uint16_t pitch_lut;
    uint8_t wave_loop;
    uint8_t wave_high;
    // Which of the ten parts this is -- the note-on preamble differs per part
    // and is the MCU's, not the ROM's.
    uint8_t idx;
    // The speed the firmware writes when the key comes up, from the record's
    // seventh byte. Velocity plays no part in it.
    uint8_t release;
    // Two curve selectors, four bits each: low nibble for the hard layer,
    // high nibble for the soft one. Sixteen curves, so four bits is exact.
    uint8_t sel;
    uint8_t ncorner;
    // The segment list exactly as it sits in the parameter ROM: six bytes an
    // entry, and the two layers overlap inside them -- the hard layer reads
    // bytes 0..3, the soft one 2..5. Destination and speed are interpolated
    // out of these at note-on, which is what makes velocity continuous rather
    // than the four sampled layers the pack used to hold. Two bytes of tail so
    // the soft layer can read its last entry. The caller keeps the blob alive.
    const uint8_t* corners;
};

struct RdnEntry {
    uint8_t note;
    uint8_t nparts;
    RdnPartDesc parts[10];
};

class RdNewEngine {
public:
    bool loadPack(const uint8_t* blob, size_t len);

    // Read one packed segment straight from the flash blob (byte-wise, no
    // alignment requirements). Access happens only at segment boundaries.
    static inline RdnSeg segAt(const uint8_t* raw, unsigned idx) {
        const uint8_t* p = raw + idx * 6u;
        return RdnSeg{ (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24),
                       p[4], p[5] };
    }

    void noteOn(uint8_t note, uint8_t vel);
    void noteOff(uint8_t note);
    void allNotesOff();   // MIDI CC 123 rescue (lost note-offs)
    int32_t renderSample();
    void renderBlock(int32_t* acc, int n); // adds n samples into acc; ONE core-1 rendezvous per block
    int activeVoices() const;

    // Voice-parallel dual-core split (core 1 renders the odd voice indices).
    static void worker_enable(RdNewEngine* eng, bool on);
    static void worker_poll();   // single poll (used by worker_loop)
    static void worker_loop();   // core-1 entry: RAM-resident forever-loop

    // Adaptive polyphony: stolen voices are masked by the new attack
    // (proper stealing -- no freezing like the old chip-side cap).
    void setVoiceLimit(uint8_t n);  // clamps 1..kVoices and actively culls excess voices (one-block fade)
    void setPitchBend(uint32_t factorQ16);  // 65536 = center; scales all part phase increments
    void setMasterTune(uint32_t factorQ16); // 65536 = center; master tune, Q16

private:
    static constexpr int kVoices = 32;   // array capacity; the LIVE limit is voice_limit_ (bridge voice governor, default 16/12 per rate)
    static constexpr int kPartsMax = 10;

    struct Part {
        const RdnPartDesc* d = nullptr;
        uint32_t sub_phase = 0;
        uint32_t env_value = 0;
        uint8_t  env_dest = 0;
        uint32_t env_b = 0;
        uint8_t  env_flags = 0;   // bit0 some_high, bit1 inv, bit2 ci
        uint8_t  env_offset = 255;
        uint32_t phase_inc = 0;
        uint32_t wave_base = 0;
        uint8_t  wave_loop_inv = 0xff;
        uint8_t  seg_idx = 0;
        bool     in_release = false;
        bool     dead = true;
        uint32_t next_t = 0xFFFFFFFFu; // due-time of next segment (RAM cache; 0xFFFFFFFF = exhausted)
        uint8_t  pitch_hi = 0;         // (pitch_lut & 0xC000) == 0xC000, cached at noteOn
        const uint8_t* chain = nullptr; // active chain (attack or release)
        uint8_t  nch = 0;
        const uint8_t* chain0 = nullptr; // this part's attack chain, for release
        uint8_t  nseg0 = 0;
    };

    // Where a voice's chains are built at note-on. The pack holds ROM corners
    // now, not finished segments, so they cannot live in flash any more. The
    // widest note in the whole bank comes to 134 segments across its ten
    // parts; 144 leaves room without being generous.
    static constexpr int kChainSegs = 144;

    struct Voice {
        uint8_t  chainbuf[kChainSegs * 6];
        bool     active = false;
        uint8_t  note = 0;
        uint32_t age = 0;
        Part     parts[kPartsMax];
        int      nparts = 0;
        const RdnEntry* entry = nullptr;
        uint32_t released_at = 0xFFFFFFFFu; // _clock at noteOff; 0xFFFFFFFF = not released
        uint8_t  killing = 0;   // 1 = fade out over the next block, then free (governor cull)
    };

    int32_t renderVoice(Voice& v, uint32_t tOn, uint32_t tRel);
    void cullToLimit();
    void applyPitchFactor();        // recompute all active parts from bend*tune
    void renderVoicesBlock(unsigned parity, unsigned parityEnabled, uint32_t clockBase, int n, int32_t* out);
    static constexpr int kBlockMax = 64;
    const RdnEntry* findEntry(uint8_t note) const;
    uint8_t buildChain(uint8_t* out, const RdnPartDesc& d,
                       uint8_t layer, uint8_t c2, uint8_t c3) const;
    void startSegment(Part& p, const RdnSeg& s);
    void advanceSegments(Part& p, uint32_t tBase);
    void releaseVoice(Voice& v);
    Voice* allocVoice();

    uint8_t voice_limit_ = kVoices;
    uint32_t bend_q16_ = 65536;     // pitch bend factor, Q16 (65536 = center)
    uint32_t tune_q16_ = 65536;     // master tune factor, Q16 (65536 = center)
    std::vector<RdnEntry> _entries;
    const uint8_t* _velmap = nullptr;   // 256 bytes, parameter ROM
    const uint8_t* _curves = nullptr;   // 16 x 64, program ROM
    const uint32_t*       _bank = nullptr;  // 4-byte packed sample bank
    uint32_t              _clock = 0;
    uint8_t               _patchId = 0;
    Voice                 _voices[kVoices];
};
