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
    printf("Summary: %d failures\n", fails);
    return fails;
}
