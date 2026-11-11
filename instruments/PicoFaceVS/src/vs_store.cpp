// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// vs_store.cpp -- flash mirror of the program bank.
//
// The flash write path is the one from veeprom.cpp, j6_patchstore.cpp and
// m1_memory.cpp, for the same reason: after an erase or program the SDK
// leaves the QMI timing at a value that is not stable at the raised clock, so
// the write runs from SRAM and restores the timing before the next fetch from
// flash.
#include "vs_store.h"

#include <cstring>

#include "provs/engine.h"

namespace {

constexpr uint32_t kMagic = 0x31425356u;    // "VSB1"
constexpr uint32_t kVersion = 1u;
constexpr uint32_t kSector = 4096u;
constexpr uint32_t kSectors = 3u;            // 12 KB reserved
constexpr int kPrograms = 100;
constexpr size_t kProgramBytes = provs::Engine::kVsProgramBytes;
constexpr size_t kDataBytes = kPrograms * kProgramBytes;

struct Header {
    uint32_t magic, version, count, crc;
    uint32_t reserved[4];
};
static_assert(sizeof(Header) == 32, "header");
static_assert(sizeof(Header) + kDataBytes <= kSectors * kSector, "fits 12 KB");

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// The image as it goes to flash: header, then the packed programs. Assembled
// in store() and written sector by sector.
uint8_t g_image[kSectors * kSector];

}  // namespace

#if defined(VS_STORE_HOST_TEST)

uint8_t vs_store_sim_flash[kSectors * kSector];
static const uint8_t* flashImage() { return vs_store_sim_flash; }
static bool writeSector(const uint8_t* buf, uint32_t s) {
    memcpy(vs_store_sim_flash + s * kSector, buf, kSector);
    return true;
}

#elif defined(PICO_ON_DEVICE) && PICO_ON_DEVICE

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico_hw.h"
#include "project_config.h"
#include "veeprom.h"

// Below the veeprom's two sectors, the one PicoFaceJ6 keeps its user patches
// in and the eight of PicoFaceM1's memory: flash outside the image survives a
// switch of instrument, so this must not write into another instrument's
// store. Derived from the veeprom layout; moving it orphans what is stored.
#define VS_STORE_FLASH_OFFSET \
    (PICO_FLASH_SIZE_BYTES - (VEEPROM_NUM_SECTORS + 1u + 8u + kSectors) * kSector)

static const uint8_t* flashImage() {
    return (const uint8_t*)(XIP_BASE + VS_STORE_FLASH_OFFSET);
}

// One sector per call, so interrupts (the audio DMA) get through between
// sectors.
static void __no_inline_not_in_flash_func(writeSectorLocked)(const uint8_t* buf, uint32_t s) {
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(VS_STORE_FLASH_OFFSET + s * kSector, kSector);
    flash_range_program(VS_STORE_FLASH_OFFSET + s * kSector, buf, kSector);
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

namespace vs {

bool Store::load() {
    const uint8_t* f = flashImage();
    if (!f) return false;
    Header h;
    memcpy(&h, f, sizeof h);
    if (h.magic != kMagic || h.version != kVersion || h.count < 1 || h.count > (uint32_t)kPrograms ||
        crc32(f + sizeof(Header), kDataBytes) != h.crc)
        return false;
    uint8_t fields[provs::Engine::kVsFields];
    for (int i = 0; i < kPrograms; ++i) {
        provs::Engine::unpackVsProgram(f + sizeof(Header) + (size_t)i * kProgramBytes, fields);
        provs::Engine::storeVsProgram(i, fields);
    }
    provs::Engine::setVsBankCount((int)h.count);
    dirty_ = 0;
    valid_ = true;
    return true;
}

void Store::touch(int slot) {
    if (slot < 0 || slot >= kPrograms) return;
    const size_t a = sizeof(Header) + (size_t)slot * kProgramBytes, b = a + kProgramBytes - 1;
    for (size_t s = a / kSector; s <= b / kSector; ++s) dirty_ |= 1u << s;
    dirty_ |= 1u;                         // the header's CRC always changes
}

void Store::touchAll() { dirty_ = (1u << kSectors) - 1u; }

bool Store::store() {
    if (!dirty_) return true;
    // The first write on a board has to lay down every sector: the CRC covers
    // them all, and the ones never written hold something else.
    if (!valid_) touchAll();
    memset(g_image, 0xFF, sizeof g_image);
    uint8_t* data = g_image + sizeof(Header);
    for (int i = 0; i < kPrograms; ++i)
        provs::Engine::packVsProgram(provs::Engine::vsProgram(i), data + (size_t)i * kProgramBytes);
    Header h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.count = (uint32_t)provs::Engine::presetCount();
    h.crc = crc32(data, kDataBytes);
    memcpy(g_image, &h, sizeof h);
    for (uint32_t s = 0; s < kSectors; ++s) {
        if (!(dirty_ & (1u << s))) continue;
        if (!writeSector(g_image + s * kSector, s)) return false;
    }
    dirty_ = 0;
    valid_ = true;
    return true;
}

}  // namespace vs
