#include "sound_chip.h"
#include "rom_tables.h"

#include <cmath>
#include <cstring>

#if !defined(TARGET_RP2350) && !defined(PICO_BUILD)
#include "rd_capture.h"
std::vector<RdCaptureEvent>* g_rd_capture       = nullptr;
uint64_t                     g_rd_capture_clock = 0;
#endif

// LUT for the address speed
static uint32_t env_table[] = {
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

// LUT for bits 5/6/7/8 of the subphase
static uint16_t addr_table[] = {0x1e0, 0x080, 0x060, 0x04d, 0x040, 0x036, 0x02d, 0x026,
                                          0x020, 0x01b, 0x016, 0x011, 0x00d, 0x00a, 0x006, 0x003};

static uint16_t s_samples_exp_ram[0x8000];

SoundChip::SoundChip(const u8 temp_model)
{
    load_samples(temp_model);
    phase_exp_table = (const uint32_t *)&rd_phase_exp_table;

    // 64KB exponent table into RAM: two lookups per active part per sample.
    memcpy(s_samples_exp_ram, rd_samples_exp_table, sizeof(s_samples_exp_ram));
    samples_exp_table = s_samples_exp_ram;
}

u8 SoundChip::read(size_t offset)
{
    return m_irq_id;
}

void RD_HOT_FUNC(SoundChip::write)(size_t offset, u8 data)
{
    uint8_t voiceI = offset / 0x100;
    uint8_t partI = offset % 0x100 / 0x10;
    uint8_t field = offset % 8;

    if (voiceI >= NUM_VOICES || partI >= PARTS_PER_VOICE_MEM || field >= 8)
    {
        // No printf in the audio IRQ path (blocking UART = audible crackle).
        return;
    }

    SA_Part &part = m_parts[voiceI][partI];

    // flags seems to be common for all parts?
    if (field == 0x6)
    {
        m_parts[voiceI][0].flags_0 = data & 1;
        m_parts[voiceI][0].flags_1 = (data >> 1) & 1;
    }
    else if (field == 0x0)
    {
        part.pitch_lut_i &= 0x00FF;
        part.pitch_lut_i |= data << 8;
        part.phase_inc_cached = phase_exp_table[part.pitch_lut_i];
        part.pitch_hi2 = ((part.pitch_lut_i & 0xC000) == 0xC000) ? 1 : 0;
    }
    else if (field == 0x1)
    {
        part.pitch_lut_i &= 0xFF00;
        part.pitch_lut_i |= data;
        part.phase_inc_cached = phase_exp_table[part.pitch_lut_i];
        part.pitch_hi2 = ((part.pitch_lut_i & 0xC000) == 0xC000) ? 1 : 0;
    }
    else if (field == 0x2)
    {
        part.wave_addr_loop = data;
        part.wave_loop_inv = (~data) & 0xff;
    }
    else if (field == 0x3)
    {
        part.wave_addr_high = data;
        part.wave_base = (uint32_t)data << 11;
    }
    else if (field == 0x4)
        part.env_dest = data;
    else if (field == 0x5)
    {
        part.env_speed = data;
        bool some_high = (data & 0x7f) != 0;
        bool inv = (data & 0x80) != 0;
        bool ci = some_high && inv;
        part.env_b_precomp = env_table[data] | (ci ? (0x7fu << 21) : 0u);
        part.env_flags_precomp = (some_high ? 1 : 0) | (inv ? 2 : 0) | (ci ? 4 : 0);
    }
    else if (field == 0x7)
        part.env_offset = data;

    active_mask_ |= (1ull << voiceI);

#if !defined(TARGET_RP2350) && !defined(PICO_BUILD)
    if (g_rd_capture)
        g_rd_capture->push_back({g_rd_capture_clock, voiceI, partI, field, data});
#endif
}

s32 RD_HOT_FUNC(SoundChip::update_subset)(unsigned parity, unsigned parityEnabled, bool* irqOut, uint8_t* irqIdOut, uint64_t* clearOut)
{
    s32 result = 0;
    
    uint64_t mask = active_mask_;
    unsigned processed = 0;
    while (mask)
    {
        unsigned voiceI = __builtin_ctzll(mask);
        mask &= mask - 1;
        // Asymmetric split: worker core (parity 1) renders ~2/3 of the voices
        // (voiceI % 3 != 0); the master core (parity 0) renders ~1/3 because it
        // also runs the 6301 interpreter, the FX chain and the UI main loop.
        if (parityEnabled && (((voiceI % 3u) != 0u ? 1u : 0u) != parity)) continue;

        bool voice_active = false;
        for (size_t partI = 0; partI < PARTS_PER_VOICE; partI++)
        {
            if (m_parts[voiceI][partI].env_value != 0 || m_parts[voiceI][partI].env_dest != 0)
            {
                voice_active = true;
                break;
            }
        }
        if (!voice_active)
        {
            *clearOut |= (1ull << voiceI);  // demote idle voice: master clears after merge
            continue;
        }
        // Safety cap only: with 16 addressable chip voices and cap 16 this never
        // triggers. Applied per subset (NOT halved -- the asymmetric split gives
        // the worker up to ~2/3 of the voices).
        if (++processed > voice_cap_)
            break;

        SA_Part &partFlags = m_parts[voiceI][0];
        // Hoisted: the compiler cannot prove m_parts writes below do not alias partFlags.
        const bool vf0 = partFlags.flags_0;
        const bool vf1 = partFlags.flags_1;
        for (size_t partI = 0; partI < PARTS_PER_VOICE; partI++)
        {
            SA_Part &part = m_parts[voiceI][partI];

            // Tail-culling: kills inaudible release tails. Without it, a glissando
            // on the acoustic-piano patches accumulates 16 voices x 10 parts of
            // inaudible but fully-computed decay -- the instrument-dependent crackle.
            // Attack-/Sustain-segments (env_dest != 0) remain untouched. The old
            // check (env_value==0) is subsumed, as 0 >> 20 == 0.
            if (part.env_dest == 0 && (part.env_value >> 20) == 0)
            {
                part.env_value = 0; // Terminate part permanently
                part.sub_phase = 0; // Reset phase for safety
                continue;
            }

            bool irq = false;

            uint32_t volume;
            uint32_t waverom_addr;
            bool ag3_sel_sample_type;
            bool ag1_phase_hi;

            // IC19
            {
                bool env_speed_some_high = (part.env_flags_precomp & 1) != 0;
                bool env_speed_inv = (part.env_flags_precomp & 2) != 0;
                bool adder1_ci = (part.env_flags_precomp & 4) != 0;

                uint32_t adder1_a = part.env_value;
                if (!vf0)
                    adder1_a = 1 << 25;
                uint32_t adder1_b = part.env_b_precomp;

                uint32_t adder3_o = 1 + (adder1_a >> 20) + part.env_offset;
                uint32_t adder3_of = adder3_o > 0xff;
                adder3_o &= 0xff;

                volume = ~(
                    (vf0 ? ((adder1_a >> 14) & 0b111111) : 0) |
                    ((adder3_o & 0b1111) << 6) |
                    (adder3_of ? ((adder3_o & 0b11110000) << 6) : 0)
                ) & 0x3fff;

                uint32_t adder1_o = adder1_a + adder1_b + (adder1_ci ? 1 : 0);
                uint32_t adder1_of = adder1_o > 0xfffffff;
                adder1_o &= 0xfffffff;

                uint32_t adder2_o = (adder1_o >> 20) + (~part.env_dest & 0xff) + 1;
                uint32_t adder2_of = adder2_o > 0xff;

                bool end_reached = env_speed_some_high && ((adder1_of != env_speed_inv) || (env_speed_inv != adder2_of));
                irq |= end_reached;

                part.env_value = end_reached ? (part.env_dest << 20) : adder1_o;
            }

            // IC9
            {
                // Bit-exact rewrite: boolean cascades flattened to mask tests,
                // invariants precomputed on register write (exhaustively verified
                // equivalent over all 2^19 addresses).
                uint32_t adder1 = (part.phase_inc_cached + part.sub_phase) & 0xffffff;
                uint32_t adder2 = 1 + (adder1 >> 16) + part.wave_loop_inv;
                bool adder2_co = adder2 > 0xff;
                adder2 &= 0xff;
                uint32_t adder1_and = !vf1 ? 0 : (adder1 & 0xffff);
                adder1_and |= (!vf1 ? 0 : (adder2_co ? adder2 : (adder1 >> 16))) << 16;

                part.sub_phase = adder1_and;
                waverom_addr = part.wave_base | ((part.sub_phase >> 9) & 0x7ff);

                ag3_sel_sample_type = ((waverom_addr & 0x1C000) != 0) ||
                                      (((waverom_addr & 0x2000) != 0) && ((waverom_addr & 0x1800) != 0));
                ag1_phase_hi = part.pitch_hi2 || ((part.sub_phase & 0xF00000) != 0) || !vf1;
            }

            // IC8
            {
                if (part.env_value != 0 && waverom_addr < 0x20000)
                {
                    const RdSampleEntry &se = samples_ilv[waverom_addr];
                    uint32_t waverom_pa = se.exp;
                    uint32_t waverom_pb = se.delta;
                    bool sign_pa = se.exp_sign != 0;
                    bool sign_pb = se.delta_sign != 0;
                    waverom_pa |= ag3_sel_sample_type ? 1 : 0;
                    waverom_pb |= ag3_sel_sample_type ? 0 : 1;

                    if (ag1_phase_hi)
                        volume |= 0b1111 << 10;

                    uint32_t tmp_1, tmp_2;

                    uint32_t adder1_o = volume + waverom_pa;
                    bool adder1_co = adder1_o > 0x3fff;
                    adder1_o &= 0x3fff;
                    if (adder1_co)
                        adder1_o |= 0x3c00;
                    tmp_1 = adder1_o;

                    uint32_t adder3_o = addr_table[(part.sub_phase >> 5) & 0xf] + (waverom_pb & 0x1ff);
                    bool adder3_of = adder3_o > 0x1ff;
                    adder3_o &= 0x1ff;
                    if (adder3_of)
                        adder3_o |= 0x1e0;
                    
                    adder1_o = volume + (adder3_o << 5);
                    adder1_co = adder1_o > 0x3fff;
                    adder1_o &= 0x3fff;
                    if (adder1_co)
                        adder1_o |= 0x3c00;
                    tmp_2 = adder1_o;
                    
                    int32_t exp_val1 = samples_exp_table[(16384 * sign_pa) + (1024 * (tmp_1 >> 10)) + (tmp_1 & 1023)];
                    int32_t exp_val2 = samples_exp_table[(16384 * sign_pb) + (1024 * (tmp_2 >> 10)) + (tmp_2 & 1023)];
                    if (sign_pa)
                        exp_val1 = exp_val1 - 0x8000;
                    if (sign_pb)
                        exp_val2 = exp_val2 - 0x8000;
                    int32_t exp_val = exp_val1 + exp_val2;
                    
                    // env_value != 0 already guaranteed by the block guard above
                    // (hoisted there to also skip the flash read for silent parts)
                    result += exp_val;
                }
            }

            if (irq && !*irqOut)
            {
                *irqIdOut = partI | (voiceI << 4);
                *irqOut = true;
            }
        }
    }

    return result;
}

void SoundChip::load_samples(const u8 temp_model)
{
    if (temp_model == 0) {
        samples_ilv = rd_samples_ilv_a;
    } else if (temp_model == 1) {
        samples_ilv = rd_samples_ilv_b;
    } else {
        samples_ilv = rd_samples_ilv_m;
    }
}


// ===========================================================================
// Dual-core worker glue: Core 1 renders the odd voices between doorbell and
// done-flag; afterwards Core 0 runs the 6301 interpreter (which may call
// write()) -- strict phase separation, no locks needed.
// ===========================================================================

#if defined(TARGET_RP2350) || defined(PICO_BUILD)
#include "hardware/sync.h"   // __dmb()
#include "pico/platform.h"   // tight_loop_contents() -- via pico.h chain
#endif

#if !defined(TARGET_RP2350) && !defined(PICO_BUILD)
  // Host build: no-op stubs for memory barrier and spin yield
  #ifndef __dmb
  #define __dmb() ((void)0)
  #endif
  #ifndef tight_loop_contents
  #define tight_loop_contents() ((void)0)
  #endif
#endif

static SoundChip*   s_worker_chip    = nullptr;
static bool         s_worker_on      = false;
static volatile uint32_t s_worker_req   = 0;
static volatile uint32_t s_worker_done  = 0;
static volatile int32_t  s_worker_sum;
static volatile uint64_t s_worker_clear;
static volatile uint8_t  s_worker_irq;       // 1 = irq, 0 = none
static volatile uint8_t  s_worker_irq_id;    // valid only when s_worker_irq == 1

void SoundChip::worker_enable(SoundChip* chip, bool on)
{
    s_worker_chip = chip;
    s_worker_on   = on;
}

void RD_HOT_FUNC(SoundChip::worker_poll)()
{
    uint32_t req = s_worker_req;
    if (req == s_worker_done)
        return;
    __dmb();  // acquire: chip state written before the doorbell must be visible

    bool    irq   = false;
    uint8_t irqId = 0;
    uint64_t clear = 0;

    int32_t sum = s_worker_chip->update_subset(1, 1, &irq, &irqId, &clear);

    s_worker_sum     = sum;
    s_worker_clear   = clear;
    s_worker_irq     = irq ? 1 : 0;
    s_worker_irq_id  = irqId;

    __dmb();
    s_worker_done = req;
}

s32 RD_HOT_FUNC(SoundChip::update)()
{
    bool    irq   = false;
    uint8_t irqId = 0;
    uint64_t clear = 0;
    s32     sum;

    if (!s_worker_on)
    {
        // Single-core legacy path: process all voices (parityEnabled == 0)
        sum = update_subset(0, 0, &irq, &irqId, &clear);
        active_mask_ &= ~clear;
        if (irq && !m_irq_triggered)
        {
            m_irq_id         = irqId;
            m_irq_triggered  = true;
        }
        return sum;
    }

    // Dual-core path: publish all chip-state writes (interpreter register
    // writes from the previous sample!) BEFORE ringing the doorbell, or the
    // worker may compute with half-written part data on the weakly-ordered M33.
    __dmb();
    s_worker_req++;

    // Master computes its own subset (even voices only)
    sum = update_subset(0, 1, &irq, &irqId, &clear);

    // Spin until the worker has finished its current request
    while (s_worker_done != s_worker_req)
        tight_loop_contents();
    __dmb();

    // Merge worker results
    sum   += s_worker_sum;
    clear |= s_worker_clear;

    // IRQ merge: pick the candidate with the smaller voice index so the
    // merged behavior matches the original ascending single-pass scan.
    bool    mergedIrq;
    uint8_t mergedIrqId;
    if (irq && s_worker_irq == 1)
    {
        if ((irqId >> 4) <= (s_worker_irq_id >> 4)) { mergedIrq = true; mergedIrqId = irqId; }
        else                                        { mergedIrq = true; mergedIrqId = s_worker_irq_id; }
    }
    else if (irq)                  { mergedIrq = true;  mergedIrqId = irqId; }
    else if (s_worker_irq == 1)    { mergedIrq = true;  mergedIrqId = s_worker_irq_id; }
    else                           { mergedIrq = false; mergedIrqId = 0; }

    active_mask_ &= ~clear;
    if (mergedIrq && !m_irq_triggered)
    {
        m_irq_id        = mergedIrqId;
        m_irq_triggered = true;
    }

    return sum;
}
