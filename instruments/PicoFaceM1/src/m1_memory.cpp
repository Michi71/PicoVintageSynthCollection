// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// m1_memory.cpp -- RAM image of the M1's memory and its flash mirror.
//
// The flash write path is the one from veeprom.cpp and j6_patchstore.cpp, for
// the same reason: after an erase or program the SDK leaves the QMI timing at
// a value that is not stable at the raised clock, so the write runs from SRAM
// and restores the timing before the next fetch from flash.
#include "m1_memory.h"

#include <string.h>

namespace {

constexpr uint32_t kMagic = 0x4D454D31u;   // "1MEM"
constexpr uint32_t kVersion = 1u;
constexpr uint32_t kSector = 4096u;
constexpr uint32_t kSectors = 8u;           // 32 KB reserved

struct Header {
    uint32_t magic, version, length, crc;
    uint32_t reserved[4];
};
static_assert(sizeof(Header) == 32, "header");
static_assert(sizeof(Header) + sizeof(m1::MemoryImage) <= kSectors * kSector, "fits 32 KB");

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// One sector at a time is assembled here: header + memory, padded with 0xFF.
uint8_t g_sector[kSector];

}  // namespace

#if defined(TARGET_RP2350) || defined(PICO_BUILD)

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico_hw.h"
#include "project_config.h"
#include "veeprom.h"

// Below the veeprom's two sectors and below the one PicoFaceJ6 keeps its user
// patches in: flash outside the image survives a switch of instrument, so the
// M1 must not write into another instrument's store. Derived from the veeprom
// layout; moving it orphans what is stored.
#define M1_MEM_FLASH_OFFSET \
    (PICO_FLASH_SIZE_BYTES - (VEEPROM_NUM_SECTORS + 1u + kSectors) * kSector)

static const uint8_t* flashImage() {
    return (const uint8_t*)(XIP_BASE + M1_MEM_FLASH_OFFSET);
}

// One sector per call, so interrupts (the audio DMA) get through between
// sectors.
static void __no_inline_not_in_flash_func(writeSectorLocked)(const uint8_t* buf, uint32_t s) {
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(M1_MEM_FLASH_OFFSET + s * kSector, kSector);
    flash_range_program(M1_MEM_FLASH_OFFSET + s * kSector, buf, kSector);
#if PICO_RP2350
    picoface_flash_after_write();
    __dsb();
    __isb();
#endif
    restore_interrupts(ints);
}

static bool writeSector(const uint8_t* buf, uint32_t s) {
    writeSectorLocked(buf, s);
    return true;
}

#else   // host: nothing persists

static const uint8_t* flashImage() { return nullptr; }
static bool writeSector(const uint8_t*, uint32_t) { return false; }

#endif

namespace m1 {

void Memory::init(const uint8_t* g, const uint8_t* k, const uint8_t* c, const uint8_t* p) {
    fG_ = g; fK_ = k; fC_ = c; fP_ = p;
    fromFlash_ = false;
    const uint8_t* f = flashImage();
    if (f) {
        Header h;
        memcpy(&h, f, sizeof h);
        if (h.magic == kMagic && h.version == kVersion && h.length == sizeof(MemoryImage) &&
            crc32(f + sizeof(Header), sizeof(MemoryImage)) == h.crc) {
            memcpy(&img_, f + sizeof(Header), sizeof(MemoryImage));
            fromFlash_ = true;
        }
    }
    if (!fromFlash_) factoryReset();
    dirty_ = 0;
}

void Memory::factoryReset() {
    memcpy(img_.global, fG_, sizeof img_.global);
    memcpy(img_.kits, fK_, sizeof img_.kits);
    memcpy(img_.combis, fC_, sizeof img_.combis);
    memcpy(img_.programs, fP_, sizeof img_.programs);
    touchAll();
}

void Memory::touch(size_t offset, size_t len) {
    if (len == 0) return;
    const size_t a = sizeof(Header) + offset, b = a + len - 1;
    for (size_t s = a / kSector; s <= b / kSector && s < kSectors; ++s) dirty_ |= 1u << s;
    dirty_ |= 1u;                         // the header's CRC always changes
}

bool Memory::store() {
    if (!dirty_) return true;
    Header h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.length = sizeof(MemoryImage);
    h.crc = crc32(reinterpret_cast<const uint8_t*>(&img_), sizeof(MemoryImage));
    const uint8_t* src = reinterpret_cast<const uint8_t*>(&img_);
    const size_t total = sizeof h + sizeof img_;
    for (uint32_t s = 0; s < kSectors; ++s) {
        if (!(dirty_ & (1u << s))) continue;
        memset(g_sector, 0xFF, sizeof g_sector);
        for (size_t i = 0; i < kSector; ++i) {
            const size_t a = s * kSector + i;
            if (a >= total) break;
            g_sector[i] = a < sizeof h ? reinterpret_cast<const uint8_t*>(&h)[a] : src[a - sizeof h];
        }
        if (!writeSector(g_sector, s)) return false;
    }
    dirty_ = 0;
    return true;
}

}  // namespace m1
