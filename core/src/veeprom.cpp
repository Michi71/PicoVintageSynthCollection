// veeprom.cpp - Virtual EEPROM over flash for RP2350 (Pico SDK) and host unit tests.
//
// Two physical flash sectors (4096 B each) are used as a circular log of 256-byte
// records (32 slots total, 16 per sector). Each record holds a 16-byte packed header
// (magic, sequence, version, length, CRC32) followed by up to 240 bytes of payload.
// On save, the next slot after the most-recent valid one is written; when a sector
// boundary is crossed the destination sector is erased first. A simple "jump to the
// other sector" rule handles the case where the candidate slot is non-blank but not
// on a sector boundary (e.g. after an interrupted write).
//
// Lock hooks:
//   During flash_range_erase/flash_range_program the XIP window is unavailable, so
//   any code executing from flash (including the audio ISR on Core 0) must be parked
//   in RAM. The lock hook is provided by main.cpp and is responsible for safely
//   parking Core 0 / disabling audio before we disable interrupts and touch flash.
//   If the hook refuses (returns false) the save is aborted without touching flash.
//   The unlock hook restores normal operation afterwards. The lock is held once
//   around the whole save (erase-if-needed + program together), not per call.

#include "veeprom.h"

#include <string.h>

static int s_lastSlot = -1;
static uint32_t s_lastSeq = 0;
static veeprom_lock_fn s_lock = 0;
static veeprom_unlock_fn s_unlock = 0;

#ifdef VEEPROM_HOST_TEST

uint8_t  veeprom_sim_flash[VEEPROM_NUM_SECTORS * VEEPROM_SECTOR_SIZE];
uint32_t veeprom_sim_erase_count[VEEPROM_NUM_SECTORS];

static const uint8_t* read_ptr(uint32_t off) {
    return &veeprom_sim_flash[off];
}

static void erase_sector(int s) {
    memset(&veeprom_sim_flash[(uint32_t)s * VEEPROM_SECTOR_SIZE], 0xFF, VEEPROM_SECTOR_SIZE);
    veeprom_sim_erase_count[s]++;
}

static void program_record(uint32_t off, const uint8_t* buf) {
    memcpy(&veeprom_sim_flash[off], buf, VEEPROM_RECORD_SIZE);
}

void veeprom_sim_reset(void) {
    memset(veeprom_sim_flash, 0xFF, sizeof(veeprom_sim_flash));
    memset(veeprom_sim_erase_count, 0, sizeof(veeprom_sim_erase_count));
    s_lastSlot = -1;
    s_lastSeq = 0;
}

#else

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "hardware/structs/qmi.h"
#include "project_config.h"

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096u
#endif

#define VEEPROM_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - VEEPROM_NUM_SECTORS * FLASH_SECTOR_SIZE)

static const uint8_t* read_ptr(uint32_t off) {
    return (const uint8_t*)(XIP_BASE + VEEPROM_FLASH_OFFSET + off);
}

// Erase (optional) + program + QMI-timing restore in one RAM-resident block:
// after flash_range_erase/program the SDK's boot2 re-init leaves M0_TIMING at a
// value that is unstable at the 444 MHz overclock, so no code may be fetched
// from flash until the timing is restored. __no_inline_not_in_flash_func
// guarantees this function (and its literals) executes from SRAM only.
static void __no_inline_not_in_flash_func(flash_write_locked)(int eraseSectorIdx, uint32_t progOff, const uint8_t* buf) {
    uint32_t ints = save_and_disable_interrupts();
    if (eraseSectorIdx >= 0) {
        flash_range_erase(VEEPROM_FLASH_OFFSET + (uint32_t)eraseSectorIdx * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    }
    flash_range_program(VEEPROM_FLASH_OFFSET + progOff, buf, VEEPROM_RECORD_SIZE);
#if PICO_RP2350
    qmi_hw->m[0].timing = PICOFACE_QMI_M0_TIMING_OC;  // undo boot2 re-init clobber
    // Returning from this SRAM-resident function is the first instruction fetch
    // to go through the QMI again, so the timing write must have landed by then.
    // __compiler_memory_barrier() only constrains the compiler; __dsb() is what
    // orders the APB write against the XIP fetches that follow.
    __dsb();
    __isb();
#endif
    restore_interrupts(ints);
}

#endif

void veeprom_set_lock_hooks(veeprom_lock_fn lock, veeprom_unlock_fn unlock) {
    s_lock = lock;
    s_unlock = unlock;
}

uint32_t veeprom_crc32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool slot_valid(int slot) {
    const uint8_t* p = read_ptr((uint32_t)slot * VEEPROM_RECORD_SIZE);
    VeepromHdr hdr;
    memcpy(&hdr, p, sizeof(hdr));
    if (hdr.magic != VEEPROM_MAGIC) return false;
    if (hdr.len > VEEPROM_MAX_PAYLOAD) return false;
    if (hdr.crc != veeprom_crc32(p + VEEPROM_HDR_SIZE, hdr.len)) return false;
    return true;
}

void veeprom_init(void) {
    s_lastSlot = -1;
    s_lastSeq = 0;
    for (int slot = 0; slot < (int)VEEPROM_SLOTS; ++slot) {
        if (!slot_valid(slot)) continue;
        const uint8_t* p = read_ptr((uint32_t)slot * VEEPROM_RECORD_SIZE);
        VeepromHdr hdr;
        memcpy(&hdr, p, sizeof(hdr));
        if (s_lastSlot < 0 || hdr.seq > s_lastSeq) {
            s_lastSlot = slot;
            s_lastSeq = hdr.seq;
        }
    }
}

bool veeprom_load(void* payload, uint16_t maxLen, uint16_t* lenOut, uint16_t* versionOut) {
    if (s_lastSlot < 0) return false;
    if (!slot_valid(s_lastSlot)) return false;
    const uint8_t* p = read_ptr((uint32_t)s_lastSlot * VEEPROM_RECORD_SIZE);
    VeepromHdr hdr;
    memcpy(&hdr, p, sizeof(hdr));
    if (hdr.len > maxLen) return false;
    memcpy((uint8_t*)payload, p + VEEPROM_HDR_SIZE, hdr.len);
    if (lenOut) *lenOut = hdr.len;
    if (versionOut) *versionOut = hdr.version;
    return true;
}

bool veeprom_save(const void* payload, uint16_t len, uint16_t version) {
    if (len > VEEPROM_MAX_PAYLOAD) return false;
    if (payload == 0) return false;

    const uint8_t* payloadBytes = (const uint8_t*)payload;

    int candidate = (s_lastSlot < 0) ? 0 : (s_lastSlot + 1) % (int)VEEPROM_SLOTS;
    const uint32_t slots_per_sector = VEEPROM_SECTOR_SIZE / VEEPROM_RECORD_SIZE; // 16

    int needErase = -1;
    if ((uint32_t)(candidate % (int)slots_per_sector) == 0u) {
        needErase = candidate / (int)slots_per_sector;
    } else {
        const uint8_t* cp = read_ptr((uint32_t)candidate * VEEPROM_RECORD_SIZE);
        bool blank = true;
        for (uint32_t i = 0; i < VEEPROM_RECORD_SIZE; ++i) {
            if (cp[i] != 0xFF) { blank = false; break; }
        }
        if (!blank) {
            int sector = candidate / (int)slots_per_sector;
            int other = (sector + 1) % (int)VEEPROM_NUM_SECTORS;
            candidate = other * (int)slots_per_sector;
            needErase = other;
        }
    }

    static uint8_t buf[VEEPROM_RECORD_SIZE] __attribute__((aligned(4)));
    memset(buf, 0xFF, VEEPROM_RECORD_SIZE);

    VeepromHdr hdr;
    hdr.magic = VEEPROM_MAGIC;
    hdr.seq = s_lastSeq + 1;
    hdr.version = version;
    hdr.len = len;
    hdr.crc = veeprom_crc32(payloadBytes, len);
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + VEEPROM_HDR_SIZE, payloadBytes, len);

#ifdef VEEPROM_HOST_TEST
    if (needErase >= 0) erase_sector(needErase);
    program_record((uint32_t)candidate * VEEPROM_RECORD_SIZE, buf);
#else
    if (s_lock) {
        if (!s_lock()) return false;
    }
    flash_write_locked(needErase, (uint32_t)candidate * VEEPROM_RECORD_SIZE, buf);
    if (s_unlock) s_unlock();
#endif

    const uint8_t* rp = read_ptr((uint32_t)candidate * VEEPROM_RECORD_SIZE);
    if (memcmp(rp, buf, VEEPROM_RECORD_SIZE) != 0) {
        return false;
    }

    s_lastSlot = candidate;
    s_lastSeq++;
    return true;
}
