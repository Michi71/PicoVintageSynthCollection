// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include <stdio.h>
#include <string.h>
#include "veeprom.h"

static int fails = 0;
#define CHECK(cond, name) do { if (cond) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); fails++; } } while (0)

static void test_empty_flash(void) {
    veeprom_sim_reset();
    veeprom_init();
    uint8_t buf[240];
    uint16_t len = 0xFFFF, ver = 0xFFFF;
    bool r = veeprom_load(buf, sizeof(buf), &len, &ver);
    CHECK(!r, "empty_flash_load_false");
}

static void test_roundtrip(void) {
    veeprom_sim_reset();
    veeprom_init();
    uint8_t payload[100];
    for (int i = 0; i < 100; i++) payload[i] = (uint8_t)(i * 7);
    CHECK(veeprom_save(payload, 100, 1), "roundtrip_save");
    uint8_t buf[240];
    uint16_t len = 0, ver = 0;
    bool r = veeprom_load(buf, sizeof(buf), &len, &ver);
    CHECK(r, "roundtrip_load_true");
    CHECK(len == 100, "roundtrip_len");
    CHECK(ver == 1, "roundtrip_ver");
    CHECK(memcmp(buf, payload, 100) == 0, "roundtrip_data");
}

static void test_latest_wins(void) {
    veeprom_sim_reset();
    veeprom_init();
    for (int k = 0; k < 5; k++) {
        uint8_t payload[32];
        memset(payload, 0, sizeof(payload));
        payload[0] = (uint8_t)k;
        CHECK(veeprom_save(payload, 32, 1), "latest_wins_save");
    }
    uint8_t buf[240];
    uint16_t len = 0, ver = 0;
    bool r = veeprom_load(buf, sizeof(buf), &len, &ver);
    CHECK(r, "latest_wins_load_true");
    CHECK(buf[0] == 4, "latest_wins_value");
}

static void test_crc32(void) {
    veeprom_sim_reset();
    veeprom_init();
    uint32_t c = veeprom_crc32("123456789", 9);
    CHECK(c == 0xCBF43926u, "crc32_known");
}

static void test_sector_wrap(void) {
    veeprom_sim_reset();
    veeprom_init();
    for (int k = 0; k < 40; k++) {
        uint8_t payload[32];
        memset(payload, 0, sizeof(payload));
        payload[0] = (uint8_t)k;
        CHECK(veeprom_save(payload, 32, 1), "sector_wrap_save");
        uint8_t buf[240];
        uint16_t len = 0, ver = 0;
        bool r = veeprom_load(buf, sizeof(buf), &len, &ver);
        CHECK(r && buf[0] == (uint8_t)k, "sector_wrap_load");
    }
    CHECK(veeprom_sim_erase_count[0] >= 1, "sector_wrap_erase0");
    CHECK(veeprom_sim_erase_count[1] >= 1, "sector_wrap_erase1");
}

static void test_corruption_falls_back(void)
{
    uint8_t buf[240];
    uint8_t data[32];
    uint16_t len, ver;

    veeprom_sim_reset();
    veeprom_init();

    memset(data, 0, sizeof(data));
    data[0] = 1;
    CHECK(veeprom_save(data, 32, 1), "save_A");

    memset(data, 0, sizeof(data));
    data[0] = 2;
    CHECK(veeprom_save(data, 32, 1), "save_B");

    int found = -1;
    for (int slot = 0; slot < 32; slot++) {
        uint32_t seq;
        memcpy(&seq, &veeprom_sim_flash[slot * 256 + 4], sizeof(seq));
        if (seq == 2) {
            found = slot;
            break;
        }
    }
    CHECK(found >= 0, "find_slot_seq2");

    veeprom_sim_flash[found * 256 + 16] ^= 0xFF;

    veeprom_init();
    CHECK(veeprom_load(buf, sizeof(buf), &len, &ver), "load_fallback");
    CHECK(buf[0] == 1, "fallback_payload");
}

static void test_torn_write_ignored(void)
{
    uint8_t buf[240];
    uint8_t data[32];
    uint16_t len, ver;

    veeprom_sim_reset();
    veeprom_init();

    memset(data, 0, sizeof(data));
    data[0] = 7;
    CHECK(veeprom_save(data, 32, 1), "save_A");

    int found = -1;
    for (int slot = 0; slot < 32; slot++) {
        uint32_t seq;
        memcpy(&seq, &veeprom_sim_flash[slot * 256 + 4], sizeof(seq));
        if (seq == 1) {
            found = slot;
            break;
        }
    }
    CHECK(found >= 0, "find_slot_seq1");

    int next = (found + 1) % 32;
    uint32_t magic = 0x50434650;
    uint32_t seq = 999;
    uint16_t version = 1;
    uint16_t slen = 32;
    uint32_t crc = 0;

    memcpy(&veeprom_sim_flash[next * 256 + 0],  &magic,   sizeof(magic));
    memcpy(&veeprom_sim_flash[next * 256 + 4],  &seq,     sizeof(seq));
    memcpy(&veeprom_sim_flash[next * 256 + 8],  &version, sizeof(version));
    memcpy(&veeprom_sim_flash[next * 256 + 10], &slen,    sizeof(slen));
    memcpy(&veeprom_sim_flash[next * 256 + 12], &crc,     sizeof(crc));

    veeprom_init();
    CHECK(veeprom_load(buf, sizeof(buf), &len, &ver), "load_valid");
    CHECK(buf[0] == 7, "valid_payload");
}

static void test_oversize_rejected(void) {
    veeprom_sim_reset();
    veeprom_init();

    uint8_t buf[241];
    memset(buf, 0, sizeof(buf));
    CHECK(!veeprom_save(buf, 241, 1), "oversize save rejected");

    uint8_t buf100[100];
    memset(buf100, 0xA5, sizeof(buf100));
    CHECK(veeprom_save(buf100, 100, 1), "100-byte save succeeds");

    uint8_t out[100];
    uint16_t lenOut, versionOut;
    CHECK(!veeprom_load(out, 50, &lenOut, &versionOut), "load maxLen 50 rejected");
}

static void test_wear_leveling_1000(void) {
    veeprom_sim_reset();
    veeprom_init();

    uint8_t payload[16];
    memset(payload, 0, sizeof(payload));
    int failures = 0;
    for (int k = 0; k < 1000; k++) {
        payload[0] = (uint8_t)(k & 0xFF);
        if (!veeprom_save(payload, 16, 1)) {
            failures++;
        }
    }
    CHECK(failures == 0, "all 1000 saves succeed");

    CHECK(veeprom_sim_erase_count[0] <= 40, "erase_count[0] <= 40");
    CHECK(veeprom_sim_erase_count[1] <= 40, "erase_count[1] <= 40");

    uint8_t out[16];
    uint16_t lenOut, versionOut;
    CHECK(veeprom_load(out, sizeof(out), &lenOut, &versionOut), "load succeeds");
    CHECK(out[0] == (uint8_t)(999 & 0xFF), "payload[0] == 999 & 0xFF");
}

// Issue #18: PicoFaceSM booted into silence on a board that had run PicoFaceJ6.
// Both instruments are at settings version 3 and a .uf2 does not erase the
// store, so J6's 72-byte record survived the flash and satisfied the core's
// "version matches, length is at least what I need" test for SM's 54 bytes.
// SM read J6's programme number and parameter array as Solina parameters.
static void test_foreign_instrument_ignored(void) {
    // PicoFaceJ6 stores its settings and the board is then flashed with SM.
    veeprom_sim_reset();
    veeprom_set_instrument("PicoFaceJ6");
    veeprom_init();
    uint8_t j6[72];
    for (int i = 0; i < 72; i++) j6[i] = (uint8_t)(i + 1);
    CHECK(veeprom_save(j6, sizeof(j6), 3), "j6 saves its record");

    // Same flash, same settings version, different instrument: nothing to see.
    veeprom_set_instrument("PicoFaceSM");
    veeprom_init();
    uint8_t buf[240];
    uint16_t len = 0, ver = 0;
    CHECK(!veeprom_load(buf, sizeof(buf), &len, &ver), "sm does not read j6 record");

    // SM writes its own, and that one comes back.
    uint8_t sm[54];
    memset(sm, 0xC3, sizeof(sm));
    CHECK(veeprom_save(sm, sizeof(sm), 3), "sm saves over the stale sector");
    CHECK(veeprom_load(buf, sizeof(buf), &len, &ver), "sm reads its own record");
    CHECK(len == 54 && memcmp(buf, sm, 54) == 0, "sm record intact");

    // And J6, flashed back on, still finds nothing of SM's.
    veeprom_set_instrument("PicoFaceJ6");
    veeprom_init();
    CHECK(!veeprom_load(buf, sizeof(buf), &len, &ver), "j6 does not read sm record");
}

int main(void) {
    test_empty_flash();
    test_roundtrip();
    test_latest_wins();
    test_crc32();
    test_sector_wrap();
    test_corruption_falls_back();
    test_torn_write_ignored();
    test_oversize_rejected();
    test_wear_leveling_1000();
    test_foreign_instrument_ignored();
    printf("Summary: %d failures\n", fails);
    return fails;
}
