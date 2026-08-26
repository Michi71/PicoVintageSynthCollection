// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// The descriptor-replay engine: own code over data the reference emulator
// captured offline. See instruments/PicoFaceRD/README.md.

#include "rd_new_engine.h"
#include "rd_types.h"     // u8/u16/u32, RD_HOT_FUNC
#include <cstring>
#include <cstdlib>

#ifdef RD_XIP_TRACE
// Host-only: defined by tools/rd_extract/build_xip_probe.sh, never by a
// firmware build. See the call site in the sample read.
extern void rd_xip_trace(unsigned waverom_addr);
#endif

// Sub-phase interpolation LUT. NOTE: a never-written non-const static gets
// promoted to flash .rodata by GCC (verified in the linker map) -- RAM
// residency is enforced by an explicit runtime copy into .bss at loadPack.
static const uint16_t addrTblRom[16] = {0x1e0,0x080,0x060,0x04d,0x040,0x036,0x02d,0x026,
                                        0x020,0x01b,0x016,0x011,0x00d,0x00a,0x006,0x003};
static uint16_t addrTbl[16];            // .bss -> guaranteed RAM
static uint32_t s_env_ram[256];         // RAM copy of rdn_env_table

// Duplicate of the chip's env_table (mechanically copied from sound_chip.cpp).
extern const uint32_t rdn_env_table[256];

// Exponent table in RAM: two lookups per audible part per sample -- reading it
// from flash costs ~2x on the RP2350 (same lesson as the v1 engine).
// We only store the base 1024-entry table: the full 64 KB rd_samples_exp_table
// satisfies table[i] == base[m] >> e (with the sign half being the bitwise NOT
// of that shifted value), so this is bit-identical to the full table.
static uint16_t s_rdn_exp_base[1024];
static bool s_rdn_exp_init = false;

static inline uint32_t rd_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t rd_le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool RdNewEngine::loadPack(const uint8_t* blob, size_t len) {
    // Header: 'RDP3' u32 version, u8 patch_id, u8 bank, u16 note_count, then
    // the 256-byte velocity map and sixteen 64-byte curves -> 1292 bytes.
    if (!blob || len < 12 + 256 + 16 * 64) return false;
    if (std::memcmp(blob, "RDP3", 4) != 0) return false;
    if (rd_le32(blob + 4) != 1) return false; // version 1

    // Deactivate all voices before rebuilding _entries / _segStore --
    // active voices would otherwise hold dangling descriptor pointers.
    for (auto& v : _voices)
    {
        v.active = false;
        v.entry  = nullptr;
        for (auto& pt : v.parts)
            pt.dead = true;
    }

    _patchId = blob[8];
    uint8_t bank = blob[9];
    if (bank == 0) _bank = rd_samples_pk4_a;
    else if (bank == 1) _bank = rd_samples_pk4_b;
    else _bank = rd_samples_pk4_m;

    if (!s_rdn_exp_init) {
        memcpy(s_rdn_exp_base, rd_samples_exp_table, sizeof(s_rdn_exp_base));
        memcpy(addrTbl, addrTblRom, sizeof(addrTbl));
        memcpy(s_env_ram, rdn_env_table, sizeof(s_env_ram));
        s_rdn_exp_init = true;
    }

    uint16_t entry_count = (uint16_t)blob[10] | ((uint16_t)blob[11] << 8);
    _velmap = blob + 12;
    _curves = blob + 12 + 256;
    _entries.clear();
    _entries.reserve(entry_count);

    // Single pass: entries reference the segments IN PLACE in the flash blob
    // (no _segStore copy -> no heap churn, no OOM on patch switches).
    size_t pos = 12 + 256 + 16 * 64;
    while (pos < len) {
        if (pos + 2 > len) return false;
        RdnEntry e;
        e.note = blob[pos++];
        uint8_t part_count = blob[pos++];
        e.nparts = part_count > 10 ? 10 : part_count;

        for (uint8_t i = 0; i < part_count; ++i) {
            if (pos + 10 > len) return false; // fixed 10-byte part header must fit
            RdnPartDesc pd;
            pd.idx = blob[pos++];
            pd.flags = blob[pos++];
            pd.env_offset = blob[pos++];
            pd.pitch_lut = rd_le16(blob + pos); pos += 2;
            pd.wave_loop = blob[pos++];
            pd.wave_high = blob[pos++];
            pd.release = blob[pos++];
            pd.sel = blob[pos++];
            uint8_t nseg = blob[pos++];
            pd.ncorner = nseg;
            // Two bytes of tail past the last entry: the soft layer reads
            // from offset 2, so its final four bytes run past the sixth.
            if (pos + (size_t)nseg * 6 + 2 > len) return false;
            pd.corners = blob + pos;
            pos += (size_t)nseg * 6 + 2;

            if (i < e.nparts) e.parts[i] = pd;
        }
        _entries.push_back(e);
    }
    return true;
}


// ---------------------------------------------------------------- velocity
// The sound CPU has no velocity layers. It interpolates every segment between
// two corner bytes in the parameter ROM, weighted by a curve it picks from the
// velocity, and the pack now carries those corners rather than four sampled
// results. RD_FIRMWARE.md has the chain; this is the same arithmetic.

// (256-w)*lo + w*hi, truncated to 16 bits and kept as its high byte -- the
// firmware's two muls, an add, and the psha that keeps only the high half.
static inline uint8_t rdn_interp(uint8_t lo, uint8_t hi, uint8_t w) {
    uint32_t v = ((256u - (uint32_t)w) * lo + (uint32_t)w * hi) & 0xFFFFu;
    return (uint8_t)(v >> 8);
}

// Levels a segment moves per sample, signed. Bit 7 means downward, which the
// chip reaches by or-ing $7f<<21 into the increment and adding a carry: that
// wraps the 28-bit accumulator into a subtraction.
static inline int32_t rdn_ramp_rate(uint8_t speed) {
    const bool stepping = (speed & 0x7F) != 0;
    const bool carry = stepping && (speed & 0x80) != 0;
    uint32_t b = s_env_ram[speed] | (carry ? (0x7Fu << 21) : 0u);
    uint32_t x = (b + (carry ? 1u : 0u) + (1u << 27)) & ((1u << 28) - 1u);
    return (int32_t)x - (int32_t)(1u << 27);
}

// How many samples the chip takes over a ramp. The +3 is the interrupt's own
// latency plus the MCU getting round to writing the next pair. Zero means the
// segment never ends by itself, and the timeline does not advance over it.
static inline uint32_t rdn_duration(uint8_t from, uint8_t to, uint8_t speed) {
    const int32_t rate = rdn_ramp_rate(speed);
    if (rate == 0) return 0;
    const int32_t distance = ((int32_t)to - (int32_t)from) << 20;
    if (distance == 0 || ((distance > 0) != (rate > 0))) return 3;
    const uint32_t ad = (uint32_t)(distance < 0 ? -distance : distance);
    const uint32_t ar = (uint32_t)(rate < 0 ? -rate : rate);
    return ad / ar + 3u;
}

// What the firmware writes before it starts on a part's list: snap the
// envelope hard downward, then freeze it. Per-part constants of the MCU, not
// of the ROM -- part 9 gets only the first, and is written first.
struct RdnPre { uint32_t t; uint8_t dest, speed; };
static const RdnPre kRdnPreamble[10][2] = {
    {{5,31,252},{7,0,0}},   {{5,31,252},{9,0,0}},   {{5,31,252},{10,0,0}},
    {{5,31,252},{12,0,0}},  {{5,31,252},{13,0,0}},  {{5,31,252},{15,0,0}},
    {{6,31,252},{16,0,0}},  {{6,31,252},{18,0,0}},  {{6,31,252},{19,0,0}},
    {{6,31,252},{0,0,0}}};
static const uint8_t kRdnPreCount[10] = {2,2,2,2,2,2,2,2,2,1};
static const uint8_t kRdnOnset[10] = {30,32,34,36,37,39,41,42,44,21};
static const uint32_t kRdnReleaseAt = 4;

static inline void rdn_put(uint8_t* d, uint32_t t, uint8_t dest, uint8_t speed) {
    d[0] = (uint8_t)t; d[1] = (uint8_t)(t >> 8);
    d[2] = (uint8_t)(t >> 16); d[3] = (uint8_t)(t >> 24);
    d[4] = dest; d[5] = speed;
}

const RdnEntry* RdNewEngine::findEntry(uint8_t note) const {
    // One entry a note now. There is nothing left to choose between: the
    // velocity is applied when the chain is built, not when it is looked up.
    for (const auto& e : _entries)
        if (e.note == note) return &e;
    return nullptr;
}

// Builds one part's chain -- preamble, the interpolated segment list, the
// release -- into the voice's buffer, and returns how many segments the attack
// chain came to. This is the note-on cost the old format paid for in flash.
uint8_t RdNewEngine::buildChain(uint8_t* out, const RdnPartDesc& d,
                                uint8_t layer, uint8_t c2, uint8_t c3) const {
    const uint8_t pre = kRdnPreCount[d.idx];
    for (uint8_t i = 0; i < pre; ++i)
        rdn_put(out + i * 6, kRdnPreamble[d.idx][i].t,
                kRdnPreamble[d.idx][i].dest, kRdnPreamble[d.idx][i].speed);

    uint8_t n = pre;
    uint32_t t = kRdnOnset[d.idx];
    int level = -1;                       // the preamble left it somewhere
    for (uint8_t i = 0; i < d.ncorner && n < kChainSegs - 1; ++i) {
        const uint8_t* g = d.corners + i * 6 + layer;
        const uint8_t dest  = rdn_interp(g[0], g[2], c3);
        const uint8_t speed = rdn_interp(g[1], g[3], c2);
        rdn_put(out + n * 6, t, dest, speed);
        ++n;
        t += rdn_duration(level < 0 ? 0 : (uint8_t)level, dest, speed);
        level = dest;
        if (dest == 0) break;             // ed89's tsta: zero ends the chain
    }
    // The release is not the chain's own ramp to nothing: the firmware writes
    // destination zero at the record's own speed when the key comes up, on a
    // time base that starts at note-off.
    rdn_put(out + n * 6, kRdnReleaseAt, 0, d.release);
    return n;
}

void RdNewEngine::startSegment(Part& p, const RdnSeg& s) {
    p.env_dest = s.dest;
    uint8_t sp = s.speed;
    bool some_high = (sp & 0x7f) != 0;
    bool inv = (sp & 0x80) != 0;
    bool ci = some_high && inv;
    p.env_b = s_env_ram[sp] | (ci ? (0x7fu << 21) : 0u);
    p.env_flags = (some_high ? 1 : 0) | (inv ? 2 : 0) | (ci ? 4 : 0);
}

// Applies all due segments, then caches the NEXT due time in RAM.
// Flash (segAt) is touched only at real segment boundaries. The function
// itself is RAM-resident: it is called from the RAM render loop, and a
// flash-resident body would cost a veneer round-trip plus XIP code fetches
// on every segment boundary.
void RD_HOT_FUNC(RdNewEngine::advanceSegments)(Part& p, uint32_t tBase)
{
    while (p.seg_idx < p.nch) {
        RdnSeg sdue = segAt(p.chain, p.seg_idx);
        if (sdue.t > tBase) { p.next_t = sdue.t; return; }
        startSegment(p, sdue);
        p.seg_idx++;
    }
    p.next_t = 0xFFFFFFFFu; // exhausted
}

void RdNewEngine::setPitchBend(uint32_t factorQ16) {
    bend_q16_ = factorQ16;
    applyPitchFactor();
}

void RdNewEngine::setMasterTune(uint32_t factorQ16) {
    tune_q16_ = factorQ16;
    applyPitchFactor();
}

void RdNewEngine::applyPitchFactor() {
    // Combined pitch factor = bend * tune, Q16*Q16>>16 == Q16.
    // Default (both 65536) yields 65536 bit-identically.
    uint32_t factor = (uint32_t)(((uint64_t)bend_q16_ * tune_q16_) >> 16);

    // Recompute every active part's phase_inc from the base table so
    // repeated bends/tunes do not accumulate rounding error. Runs on core 0
    // between blocks (same safety domain as noteOn).
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = _voices[i];
        if (!v.active) continue;
        for (int pi = 0; pi < v.nparts; ++pi) {
            Part& p = v.parts[pi];
            if (p.d == nullptr) continue;
            p.phase_inc = (uint32_t)(((uint64_t)rd_phase_exp_table[p.d->pitch_lut] * factor) >> 16);
        }
    }
}

void RdNewEngine::setVoiceLimit(uint8_t n)
{
    voice_limit_ = (n < 1) ? 1 : (n > kVoices ? kVoices : n);
    cullToLimit();
}

// Active culling: when the limit drops below the active count, mark excess
// voices for a one-block fade-out. Victim preference mirrors allocVoice:
// oldest voice with any part still in its release stage; otherwise oldest
// active voice overall. Already-killing voices are skipped so we never
// pile two culls onto the same slot.
void RdNewEngine::cullToLimit()
{
    int count = 0;
    for (int i = 0; i < kVoices; ++i)
        if (_voices[i].active && !_voices[i].killing) ++count;

    while (count > voice_limit_) {
        int victim = -1;
        uint32_t bestAge = 0xFFFFFFFFu;

        // First pass: oldest voice with a part in release.
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = _voices[i];
            if (!v.active || v.killing) continue;
            bool inRel = false;
            for (int p = 0; p < v.nparts; ++p)
                if (v.parts[p].in_release) { inRel = true; break; }
            if (!inRel) continue;
            if (v.age <= bestAge) { bestAge = v.age; victim = i; }
        }

        // Second pass (only if none in release): oldest active voice overall.
        if (victim < 0) {
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = _voices[i];
                if (!v.active || v.killing) continue;
                if (v.age <= bestAge) { bestAge = v.age; victim = i; }
            }
        }

        if (victim < 0) break; // safety: nothing eligible

        _voices[victim].killing = 1;
        --count;
    }
}

RdNewEngine::Voice* RdNewEngine::allocVoice() {
    int active_count = 0;
    for (int i = 0; i < kVoices; ++i)
        if (_voices[i].active) active_count++;

    // Below the (patch-adaptive) limit: hand out a free slot.
    if (active_count < (int)voice_limit_) {
        for (int i = 0; i < kVoices; ++i)
            if (!_voices[i].active) return &_voices[i];
    }

    // At the limit: steal. Prefer the oldest releasing voice.
    Voice* oldest = nullptr;
    uint32_t min_age = 0xFFFFFFFFu;
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = _voices[i];
        if (!v.active) continue;
        for (int pi = 0; pi < v.nparts; ++pi) {
            if (v.parts[pi].in_release) {
                if (v.age < min_age) { min_age = v.age; oldest = &v; }
                break;
            }
        }
    }
    if (oldest) return oldest;

    min_age = 0xFFFFFFFFu;
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = _voices[i];
        if (v.active && v.age < min_age) { min_age = v.age; oldest = &v; }
    }
    return oldest;
}

void RdNewEngine::noteOn(uint8_t note, uint8_t vel) {
    // Same-note retrigger: release the previous strike of this key first (real
    // re-strike behavior; also self-heals a lost note-off -- essential for
    // sustaining patches like the Clavi, which never decay on their own).
    for (auto& v : _voices) {
        if (v.active && v.note == note && v.released_at == 0xFFFFFFFFu)
            releaseVoice(v);
    }

    const RdnEntry* e = findEntry(note);
    if (!e) return;
    Voice* v = allocVoice();
    if (!v) return;

    // The velocity, the way the sound CPU takes it: a byte out of the
    // parameter ROM's map, whose top bit picks the hard or soft layer while
    // the rest becomes the straight weight and, shifted, an index into one of
    // sixteen curves. Two weights come out -- the curved one moves the
    // destinations, the straight one the speeds.
    const uint8_t c0 = _velmap[vel & 0x7F];
    const uint8_t layer = (c0 & 0x80) ? 2 : 0;
    const uint8_t c2 = (uint8_t)((c0 << 1) & 0xFF);
    const uint8_t c1 = (uint8_t)(c2 >> 2);
    uint32_t chain_at = 0;

    const bool wasKilling = v->killing != 0;
    v->active = true;
    v->killing = 0;             // a stolen slot must not carry a pending cull
    v->note = note;
    v->age = _clock;
    v->nparts = e->nparts;
    v->entry = e;

    for (int i = 0; i < e->nparts; ++i) {
        Part& p = v->parts[i];
        p.sub_phase = 0;
        p.env_value = 0;
        p.seg_idx = 0;
        p.in_release = false;
        p.dead = false;

        const RdnPartDesc* d = &e->parts[i];
        p.d = d;
        p.env_offset = d->env_offset;
        p.phase_inc = rd_phase_exp_table[d->pitch_lut];
        {
            uint32_t factor = (uint32_t)(((uint64_t)bend_q16_ * tune_q16_) >> 16);
            if (factor != 65536) {
                p.phase_inc = (uint32_t)(((uint64_t)rd_phase_exp_table[d->pitch_lut] * factor) >> 16);
            }
        }
        p.wave_base = (uint32_t)d->wave_high << 11;
        p.wave_loop_inv = (~d->wave_loop) & 0xff;

        // Timeline replay: the part waits (env idle) until its first
        // captured segment write becomes due -- no immediate start.
        p.env_dest = 0;
        p.env_b = 0;
        p.env_flags = 0;
        p.pitch_hi = ((d->pitch_lut & 0xC000) == 0xC000) ? 1 : 0;

        // The chain is built here rather than read from flash: the pack holds
        // the ROM's corners, and this is where the velocity is applied.
        const uint8_t sel = layer ? (uint8_t)(d->sel & 0x0F)
                                  : (uint8_t)(d->sel >> 4);
        const uint8_t c3 = _curves[(size_t)(sel & 0x0F) * 64 + c1];
        uint8_t* dst = v->chainbuf + chain_at * 6;
        const uint32_t room = (uint32_t)kChainSegs - chain_at;
        uint8_t built = 0;
        if (room >= (uint32_t)d->ncorner + 4u)
            built = buildChain(dst, *d, layer, c2, c3);
        p.chain0 = dst;
        p.nseg0 = built;
        p.chain = dst;
        p.nch = built;
        chain_at += built + 1u;          // the release sits right behind
        p.next_t = 0xFFFFFFFFu;
        p.dead = (built == 0);
        if (!p.dead && p.nch > 0)
            advanceSegments(p, 0);   // apply any t=0 segments, prime next_t
    }
    v->released_at = 0xFFFFFFFFu;
    if (wasKilling) cullToLimit(); // re-condemn an older voice to keep the condemned population constant
}

void RdNewEngine::releaseVoice(Voice& v) {
    if (v.released_at != 0xFFFFFFFFu) return; // already releasing

    for (int i = 0; i < v.nparts; ++i) {
        Part& p = v.parts[i];
        if (p.in_release) continue;
        p.in_release = true;
        p.seg_idx = 0;
        // The release sits right behind this part's attack chain, in the
        // voice's own buffer -- both were written at note-on.
        p.chain = p.chain0 + (size_t)p.nseg0 * 6;
        p.nch = 1;
        p.next_t = 0xFFFFFFFFu;
        if (p.chain0 == nullptr) {
            RdnSeg fast_rel = {0, 0, 200};
            startSegment(p, fast_rel);
        } else {
            advanceSegments(p, 0);  // apply t=0 release writes, prime next_t
        }
    }
    v.released_at = _clock;
}

void RdNewEngine::noteOff(uint8_t note) {
    for (auto& v : _voices) {
        if (v.active && v.note == note) releaseVoice(v);
    }
}

// MIDI CC 123 rescue: releases every voice through its normal release chain
// -- the panic path for lost note-offs.
void RdNewEngine::allNotesOff()
{
    for (auto& v : _voices) {
        if (v.active) releaseVoice(v);
    }
}

int RdNewEngine::activeVoices() const {
    int count = 0;
    for (const auto& v : _voices) {
        if (v.active) count++;
    }
    return count;
}

// ==== Part 2: rendering ====

int32_t RD_HOT_FUNC(RdNewEngine::renderVoice)(Voice& v, uint32_t tOn, uint32_t tRel) {
    int32_t result = 0;
    bool all_dead = true;
    for (int pi = 0; pi < v.nparts; ++pi) {
        Part& p = v.parts[pi];
        if (p.dead) continue;
        all_dead = false;

        // Timeline replay via RAM cache: flash is only touched when a
        // segment actually becomes due (advanceSegments).
        uint32_t tBase = p.in_release ? tRel : tOn;
        if (tBase >= p.next_t)
            advanceSegments(p, tBase);

        // Safety net (generalized): once released and the chain is exhausted,
        // the part MUST fade to zero. Covers (a) truncated chains ending at
        // dest != 0 and (b) chains ending on a FREEZE segment (speed & 0x7F
        // == 0 -> env_b = 0, end_reached can never fire): instrument 4 has 22
        // release chains ending on (dest=0, speed=0), which froze the env at
        // an audible level whenever the played velocity fell between the
        // captured layers -- the immortal-voice bug (A stuck at 10-11).
        // startSegment(190) sets some_high, so the condition self-clears.
        if (p.in_release && p.seg_idx >= p.nch && (p.env_value >> 20) != 0 &&
            (p.env_dest != 0 || (p.env_flags & 1) == 0)) {
            startSegment(p, RdnSeg{0, 0, 190});
        }

        // Gate: frozen (env AND phase) until the first segment is due.
        if (p.seg_idx == 0 && !p.in_release)
            continue;

        // Tail-cull only once the current chain is exhausted.
        if ((p.seg_idx > 0 || p.in_release) && p.seg_idx >= p.nch &&
            p.env_dest == 0 && (p.env_value >> 20) == 0) {
            p.env_value = 0;
            p.sub_phase = 0;
            p.dead = true;
            continue;
        }

        // IC19 (vf0=true)
        bool some_high = (p.env_flags & 1) != 0;
        bool inv = (p.env_flags & 2) != 0;
        bool ci = (p.env_flags & 4) != 0;
        uint32_t adder1_a = p.env_value;
        uint32_t adder1_o = adder1_a + p.env_b + (ci ? 1 : 0);
        bool adder1_of = adder1_o > 0xfffffff;
        adder1_o &= 0xfffffff;
        uint32_t adder2_o = (adder1_o >> 20) + (~p.env_dest & 0xff) + 1;
        bool adder2_of2 = adder2_o > 0xff;
        bool end_reached = some_high && ((adder1_of != inv) || (inv != adder2_of2));
        p.env_value = end_reached ? ((uint32_t)p.env_dest << 20) : adder1_o;

        // IC9 (vf1=true)
        uint32_t adder1 = (p.phase_inc + p.sub_phase) & 0xffffff;
        uint32_t adder2 = 1 + (adder1 >> 16) + p.wave_loop_inv;
        bool adder2_co = adder2 > 0xff;
        adder2 &= 0xff;
        uint32_t adder1_and = (adder1 & 0xffff) | ((adder2_co ? adder2 : (adder1 >> 16)) << 16);
        p.sub_phase = adder1_and;
        uint32_t waverom_addr = p.wave_base | ((p.sub_phase >> 9) & 0x7ff);
        bool ag3 = ((waverom_addr & 0x1C000) != 0) || (((waverom_addr & 0x2000) != 0) && ((waverom_addr & 0x1800) != 0));
        bool ag1 = p.pitch_hi || ((p.sub_phase & 0xF00000) != 0);

        // IC8
        if (p.env_value != 0 && waverom_addr < 0x20000) {
        // Volume mapping (moved here: only the IC8 path consumes it).
        uint32_t adder3_o = 1 + (adder1_a >> 20) + p.env_offset;
        bool adder3_of = adder3_o > 0xff;
        adder3_o &= 0xff;
        uint32_t volume = ~(((adder1_a >> 14) & 0b111111) | ((adder3_o & 0b1111) << 6) | (adder3_of ? ((adder3_o & 0b11110000) << 6) : 0)) & 0x3fff;

        // 4-byte packed entry: one 32-bit load, two entries per XIP line.
        // This single load is the whole flash story: one per part per sample,
        // so up to 120 of them at twelve voices. tools/rd_extract runs the
        // address stream through a cache model to see how many of them miss;
        // the hook is compile-time and absent from every firmware build.
#ifdef RD_XIP_TRACE
        rd_xip_trace(waverom_addr);
#endif
        const uint32_t w = _bank[waverom_addr];
        uint32_t pa = (w & 0x3FFFu) | (ag3 ? 1u : 0u);
        uint32_t pb = ((w >> 15) & 0x1FFu) | (ag3 ? 0u : 1u);
        bool sign_pa = ((w >> 14) & 1u) != 0;
        bool sign_pb = ((w >> 24) & 1u) != 0;
        uint32_t vol = volume;
        if (ag1) vol |= 0b1111 << 10;
        uint32_t t1 = vol + pa;
        bool c1 = t1 > 0x3fff;
        t1 &= 0x3fff;
        if (c1) t1 |= 0x3c00;
        uint32_t a3 = addrTbl[(p.sub_phase >> 5) & 0xf] + (pb & 0x1ff);
        bool a3of = a3 > 0x1ff;
        a3 &= 0x1ff;
        if (a3of) a3 |= 0x1e0;
        uint32_t t2 = vol + (a3 << 5);
        bool c2 = t2 > 0x3fff;
        t2 &= 0x3fff;
        if (c2) t2 |= 0x3c00;
        int32_t v1 = s_rdn_exp_base[t1 & 1023] >> (t1 >> 10);
        int32_t e1 = sign_pa ? ~v1 : v1;
        int32_t v2 = s_rdn_exp_base[t2 & 1023] >> (t2 >> 10);
        int32_t e2 = sign_pb ? ~v2 : v2;
        result += e1 + e2;
        }
    }
    if (all_dead) {
        v.active = false;
    }
    return result;
}

// Core-1 dispatch loop MUST live in RAM: a flash-resident loop (plus the
// long-branch veneer into RAM worker_poll) adds doorbell jitter and steals
// QSPI slots from the sample-ROM stream (verified via disassembly).
void RD_HOT_FUNC(RdNewEngine::worker_loop)()
{
    for (;;) worker_poll();
}

// ===========================================================================
// Voice-parallel dual-core worker (same doorbell protocol as the proven
// SoundChip split: publish-barrier BEFORE the doorbell, acquire after it).
// ===========================================================================

static RdNewEngine* s_rdn_worker_eng = nullptr;
static bool s_rdn_worker_on = false;
static volatile uint32_t s_rdn_req = 0, s_rdn_done = 0;
static volatile uint32_t s_rdn_clock_base = 0;
static volatile int32_t s_rdn_n = 0;
static int32_t s_rdn_worker_buf[64]; // worker-owned accumulation buffer (block path)

#if !defined(TARGET_RP2350) && !defined(PICO_BUILD)
#ifndef __dmb
#define __dmb() __sync_synchronize()
#endif
#ifndef tight_loop_contents
#define tight_loop_contents() ((void)0)
#endif
#else
#include "hardware/sync.h"
#endif

void RdNewEngine::worker_enable(RdNewEngine* eng, bool on)
{
    s_rdn_worker_eng = eng;
    s_rdn_worker_on = on;
    s_rdn_req = 0;
    s_rdn_done = 0;
}

void RD_HOT_FUNC(RdNewEngine::worker_poll)()
{
    if (!s_rdn_worker_eng) return;
    uint32_t req = s_rdn_req;
    if (s_rdn_done == req) return;
    __dmb();  // acquire: block params + voice state written before the doorbell
    int n = s_rdn_n;
    uint32_t cb = s_rdn_clock_base;
    for (int k = 0; k < n; ++k) s_rdn_worker_buf[k] = 0;
    s_rdn_worker_eng->renderVoicesBlock(1, 1, cb, n, s_rdn_worker_buf);
    __dmb();  // publish buffer before signalling done
    s_rdn_done = req;
}

int32_t RD_HOT_FUNC(RdNewEngine::renderSample)()
{
    // Compatibility wrapper (host tools): one-sample block.
    int32_t acc = 0;
    renderBlock(&acc, 1);
    return acc;
}


// Mechanical copy of sound_chip.cpp env_table (keep in sync).
const uint32_t rdn_env_table[256] = {
    0x000000, 0x000023, 0x000026, 0x000029, 0x00002d, 0x000031, 0x000036,
    0x00003b, 0x000040, 0x000046, 0x00004c, 0x000052, 0x00005a, 0x000062,
    0x00006c, 0x000076, 0x000080, 0x00008c, 0x000098, 0x0000a4, 0x0000b4,
    0x0000c4, 0x0000d8, 0x0000ec, 0x000104, 0x00011c, 0x000134, 0x00014c,
    0x00016c, 0x00018c, 0x0001b4, 0x0001dc, 0x000200, 0x000230, 0x000260,
    0x000290, 0x0002d0, 0x000310, 0x000360, 0x0003b0, 0x000400, 0x000460,
    0x0004c0, 0x000520, 0x0005a0, 0x000620, 0x0006c0, 0x000760, 0x000800,
    0x0008c0, 0x000980, 0x000a40, 0x000b40, 0x000c40, 0x000d80, 0x000ec0,
    0x001000, 0x001180, 0x001300, 0x001480, 0x001680, 0x001880, 0x001b00,
    0x001d80, 0x002000, 0x002300, 0x002600, 0x002900, 0x002d00, 0x003100,
    0x003600, 0x003b00, 0x004000, 0x004600, 0x004c00, 0x005200, 0x005a00,
    0x006200, 0x006c00, 0x007600, 0x008000, 0x008c00, 0x009800, 0x00a400,
    0x00b400, 0x00c400, 0x00d800, 0x00ec00, 0x010000, 0x011800, 0x013000,
    0x014800, 0x016800, 0x018800, 0x01b000, 0x01d800, 0x020000, 0x023000,
    0x026000, 0x029000, 0x02d000, 0x031000, 0x036000, 0x03b000, 0x040000,
    0x046000, 0x04c000, 0x052000, 0x05a000, 0x062000, 0x06c000, 0x076000,
    0x080000, 0x08c000, 0x098000, 0x0a4000, 0x0b4000, 0x0c4000, 0x0d8000,
    0x0ec000, 0x100000, 0x118000, 0x130000, 0x148000, 0x168000, 0x188000,
    0x1b0000, 0x1d8000, 0x000000, 0x1fffdc, 0x1fffd9, 0x1fffd6, 0x1fffd2,
    0x1fffce, 0x1fffc9, 0x1fffc4, 0x1fffbf, 0x1fffb9, 0x1fffb3, 0x1fffad,
    0x1fffa5, 0x1fff9d, 0x1fff93, 0x1fff89, 0x1fff7f, 0x1fff73, 0x1fff67,
    0x1fff5b, 0x1fff4b, 0x1fff3b, 0x1fff27, 0x1fff13, 0x1ffefb, 0x1ffee3,
    0x1ffecb, 0x1ffeb3, 0x1ffe93, 0x1ffe73, 0x1ffe4b, 0x1ffe23, 0x1ffdff,
    0x1ffdcf, 0x1ffd9f, 0x1ffd6f, 0x1ffd2f, 0x1ffcef, 0x1ffc9f, 0x1ffc4f,
    0x1ffbff, 0x1ffb9f, 0x1ffb3f, 0x1ffadf, 0x1ffa5f, 0x1ff9df, 0x1ff93f,
    0x1ff89f, 0x1ff7ff, 0x1ff73f, 0x1ff67f, 0x1ff5bf, 0x1ff4bf, 0x1ff3bf,
    0x1ff27f, 0x1ff13f, 0x1fefff, 0x1fee7f, 0x1fecff, 0x1feb7f, 0x1fe97f,
    0x1fe77f, 0x1fe4ff, 0x1fe27f, 0x1fdfff, 0x1fdcff, 0x1fd9ff, 0x1fd6ff,
    0x1fd2ff, 0x1fceff, 0x1fc9ff, 0x1fc4ff, 0x1fbfff, 0x1fb9ff, 0x1fb3ff,
    0x1fadff, 0x1fa5ff, 0x1f9dff, 0x1f93ff, 0x1f89ff, 0x1f7fff, 0x1f73ff,
    0x1f67ff, 0x1f5bff, 0x1f4bff, 0x1f3bff, 0x1f27ff, 0x1f13ff, 0x1effff,
    0x1ee7ff, 0x1ecfff, 0x1eb7ff, 0x1e97ff, 0x1e77ff, 0x1e4fff, 0x1e27ff,
    0x1dffff, 0x1dcfff, 0x1d9fff, 0x1d6fff, 0x1d2fff, 0x1cefff, 0x1c9fff,
    0x1c4fff, 0x1bffff, 0x1b9fff, 0x1b3fff, 0x1adfff, 0x1a5fff, 0x19dfff,
    0x193fff, 0x189fff, 0x17ffff, 0x173fff, 0x167fff, 0x15bfff, 0x14bfff,
    0x13bfff, 0x127fff, 0x113fff, 0x0fffff, 0x0e7fff, 0x0cffff, 0x0b7fff,
    0x097fff, 0x077fff, 0x04ffff, 0x027fff};

void RD_HOT_FUNC(RdNewEngine::renderVoicesBlock)(unsigned parity, unsigned parityEnabled,
                                                 uint32_t clockBase, int n, int32_t* out)
{
    // Voice-outer / sample-inner: part state stays in registers and the
    // sample-ROM reads of one part become sequential within the block.
    // Killing voices (governor cull) are faded out over this single block
    // with a linear ramp (n-1-k)/n, then freed: ~3 ms, click-free, and the
    // CPU relief lands right after this block instead of after natural decay.
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = _voices[i];
        if (!v.active) continue;
        if (parityEnabled && (((unsigned)i & 1u) != parity)) continue;

        if (v.killing) {
            // Reciprocal multiply avoids __aeabi_ldivmod in the hot path.
            const uint32_t recip = (1u << 16) / (uint32_t)n;
            // One-block linear fade-out, then free the slot.
            for (int k = 0; k < n; ++k) {
                if (!v.active) break;
                uint32_t now = clockBase + (uint32_t)k;
                uint32_t tRel = (v.released_at != 0xFFFFFFFFu) ? (now - v.released_at) : 0;
                out[k] += (int32_t)(((int64_t)renderVoice(v, now - v.age, tRel) * (int64_t)((uint32_t)(n - 1 - k) * recip)) >> 16);
            }
            v.active  = false;
            v.killing = 0;
            continue;
        }

        for (int k = 0; k < n; ++k) {
            if (!v.active) break;
            uint32_t now = clockBase + (uint32_t)k;
            uint32_t tRel = (v.released_at != 0xFFFFFFFFu) ? (now - v.released_at) : 0;
            out[k] += renderVoice(v, now - v.age, tRel);
        }
    }
}

void RD_HOT_FUNC(RdNewEngine::renderBlock)(int32_t* acc, int n)
{
    if (n > kBlockMax) n = kBlockMax;
    if (!s_rdn_worker_on) {
        renderVoicesBlock(0, 0, _clock, n, acc);
        _clock += (uint32_t)n;
        return;
    }

    // Dual-core: ONE rendezvous per block.
    s_rdn_clock_base = _clock;
    s_rdn_n = n;
    __dmb();                 // publish block params + all state before the doorbell
    s_rdn_req++;

    renderVoicesBlock(0, 1, _clock, n, acc);   // core 0: even voices

    while (s_rdn_done != s_rdn_req) tight_loop_contents();
    __dmb();                 // acquire the worker's buffer

    for (int k = 0; k < n; ++k) acc[k] += s_rdn_worker_buf[k];
    _clock += (uint32_t)n;
}

