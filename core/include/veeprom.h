// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/veeprom.h
//
// Virtual EEPROM for PicoFaceDX firmware.
//
// Purpose:
//   Persistent settings storage implemented as a wear-leveled append log
//   stored in the LAST two 4 KB sectors of the 16 MB external flash.
//
// Flash layout:
//   - Two sectors of 4096 bytes each (ping-pong erase scheme).
//   - Each sector is divided into fixed-size records of 256 bytes.
//   - Each record begins with a 16-byte header (VeepromHdr) followed by
//     up to 240 bytes of payload.
//   - Records are appended sequentially within the active sector. When a
//     sector fills, the other sector is erased and writing continues there.
//   - The latest valid record (matching magic + valid CRC) is selected by
//     the highest sequence number found during veeprom_init().
//
// Concurrency:
//   Flash erase/program on RP2350 disables XIP, so any code running on the
//   other core must be RAM-resident or parked for the duration. Lock/unlock
//   hooks are registered via veeprom_set_lock_hooks() and invoked around
//   erase/program. Every instrument writes from core0 between two audio
//   blocks and install no-ops; the hooks exist for an instrument that keeps a
//   worker on core1 which is not RAM-resident.
//
#pragma once

#include <stdint.h>
#include <stddef.h>

// ----- Configuration constants ------------------------------------------------

#define VEEPROM_NUM_SECTORS   2u
#define VEEPROM_SECTOR_SIZE   4096u
#define VEEPROM_RECORD_SIZE   256u
#define VEEPROM_SLOTS         ((VEEPROM_NUM_SECTORS * VEEPROM_SECTOR_SIZE) / VEEPROM_RECORD_SIZE)
#define VEEPROM_HDR_SIZE      16u
#define VEEPROM_MAX_PAYLOAD   (VEEPROM_RECORD_SIZE - VEEPROM_HDR_SIZE)
// Base magic, "PCFP". The value actually written and checked is this mixed
// with the instrument name -- see veeprom_set_instrument().
#define VEEPROM_MAGIC         0x50434650u   // "PCFP"

// ----- Record header ----------------------------------------------------------

// On-flash record header. CRC is computed over the payload bytes only.
struct __attribute__((packed)) VeepromHdr {
    uint32_t magic;    // VEEPROM_MAGIC for a valid record
    uint32_t seq;      // monotonically increasing sequence number
    uint16_t version;  // application-defined schema version
    uint16_t len;      // payload length in bytes (<= VEEPROM_MAX_PAYLOAD)
    uint32_t crc;      // CRC-32 over payload bytes only
};

static_assert(sizeof(VeepromHdr) == VEEPROM_HDR_SIZE, "VeepromHdr must be exactly 16 bytes");

// ----- Core-parking hooks -----------------------------------------------------

// Park the other core before flash erase/program; returns true on success.
typedef bool (*veeprom_lock_fn)(void);

// Release the other core after flash erase/program completes.
typedef void (*veeprom_unlock_fn)(void);

// Register core-parking hooks used by the flash backend around erase/program.
void veeprom_set_lock_hooks(veeprom_lock_fn lock, veeprom_unlock_fn unlock);

// ----- Public API -------------------------------------------------------------

// Tie every record to one instrument. MUST be called before veeprom_init().
//
// The store sits at a fixed address at the top of flash and a .uf2 does not
// erase it, so flashing a different instrument onto the same board leaves the
// previous one's record in place. The header carries a schema version but no
// instrument, and those versions collide across the collection -- PicoFaceJ6,
// PicoFaceSM and PicoFaceDX all reached version 3 independently. The core
// accepts a record whose version matches and whose length is at least what the
// instrument needs, so a 72-byte J6 record satisfied PicoFaceSM's 54, and
// PicoFaceSM read J6's programme number, MIDI channel and parameter array as
// its own. Reported as "starts with a silent preset" (issue #18), and silence
// is the polite outcome of that.
//
// The name is folded into the magic, so a record from another instrument fails
// the same check a corrupt one does and its slot is reclaimed. Records written
// before this existed no longer validate either: settings are lost once, which
// is what a schema change has always cost here.
void veeprom_set_instrument(const char* name);

// Scan flash and locate the latest valid record. Must be called once at boot.
void veeprom_init(void);

// Copy the payload of the latest valid record into `payload`.
// `lenOut` and `versionOut` may be NULL. Returns false if no valid record
// exists or if the stored payload length exceeds `maxLen`.
bool veeprom_load(void* payload, uint16_t maxLen, uint16_t* lenOut, uint16_t* versionOut);

// Append a new record containing `len` bytes of `payload` with the given
// `version`. Erases the alternate sector on wrap. Returns false on failure.
bool veeprom_save(const void* payload, uint16_t len, uint16_t version);

// Compute reflected CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF),
// bytewise loop without a lookup table.
uint32_t veeprom_crc32(const void* data, size_t len);

// ----- Host-test introspection ------------------------------------------------

#ifdef VEEPROM_HOST_TEST

// Simulated flash image and per-sector erase counters for host testing.
extern uint8_t  veeprom_sim_flash[VEEPROM_NUM_SECTORS * VEEPROM_SECTOR_SIZE];
extern uint32_t veeprom_sim_erase_count[VEEPROM_NUM_SECTORS];

// Reset simulated flash to 0xFF, zero erase counters, and reset scan state.
void veeprom_sim_reset(void);

#endif // VEEPROM_HOST_TEST
