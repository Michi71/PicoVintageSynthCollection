// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from giulioz/rdpiano and MAME; copyright is shared with their authors.
// See instruments/PicoFaceRD/README.md.

#ifndef SOUND_CHIP_H
#define SOUND_CHIP_H

#include <stdio.h>
#include "mame_utils.h"
#include "rom_tables.h"

class SoundChip {
public:
  SoundChip(const u8 temp_model);

  u8 read(size_t offset);
  void write(size_t offset, u8 data);

  s32 update();

  // Dual-core split: subset pass over active_mask_ (read-only on the mask).
  // parityEnabled!=0 -> only voices with (voiceI & 1) == parity.
  // Voice bits to clear are reported via *clearOut, IRQ candidate via *irqOut/*irqIdOut.
  s32 update_subset(unsigned parity, unsigned parityEnabled, bool* irqOut, uint8_t* irqIdOut, uint64_t* clearOut);

  void set_voice_cap(uint8_t cap) { voice_cap_ = cap; }

  static void worker_poll();   // Core-1 entry: call in a tight loop
  static void worker_enable(SoundChip* chip, bool on);

  void load_samples(const u8 temp_model);

  // if there is an IRQ currently waiting
  bool m_irq_triggered = false;

private:
  static constexpr unsigned NUM_VOICES = 64;
  static constexpr unsigned PARTS_PER_VOICE = 10;
  static constexpr unsigned PARTS_PER_VOICE_MEM = 64;

  const RdSampleEntry *samples_ilv;   // interleaved exp/delta/signs: one XIP burst per part

  uint64_t active_mask_ = 0;          // voices touched by writes; cleared when found inactive
  static constexpr unsigned MAX_ACTIVE_VOICES = 16; // full 16 like the original hardware (20 kHz patches)
  uint8_t voice_cap_ = 16; // adaptive: lowered for 32 kHz patches (tighter per-sample budget)

  const uint32_t *phase_exp_table;
  const uint16_t *samples_exp_table;

  struct SA_Part {
    uint32_t sub_phase = 0;
    uint32_t env_value = 0;

    uint16_t pitch_lut_i;
    uint32_t phase_inc_cached = 0;  // phase_exp_table[pitch_lut_i], updated on write
    uint32_t env_b_precomp = 0;     // env_table[env_speed] | carry-extension, updated on env_speed write
    uint8_t env_flags_precomp = 0;  // bit0 = env_speed_some_high, bit1 = env_speed_inv, bit2 = adder1_ci
    uint8_t wave_loop_inv = 0xff;   // (~wave_addr_loop)&0xff, precomputed on write (0xff matches unwritten wave_addr_loop=0)
    uint32_t wave_base = 0;         // wave_addr_high << 11, precomputed on write
    uint8_t pitch_hi2 = 0;          // BIT15&&BIT14 of pitch_lut_i, precomputed on pitch writes
    uint8_t wave_addr_loop;
    uint8_t wave_addr_high;
    uint8_t env_dest;
    uint8_t env_speed;
    bool flags_0;
    bool flags_1;
    uint8_t env_offset;
  };

  SA_Part m_parts[NUM_VOICES][PARTS_PER_VOICE_MEM];    // channel memory
  uint8_t m_irq_id = 0;						                     // voice/part that triggered the IRQ
};

#endif
