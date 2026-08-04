#include "rom_tables.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>

// little-endian fwrite helpers
static void w16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void w32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }

static const RdSampleEntry* get_bank(const char* b) {
    if (!strcmp(b, "a")) return rd_samples_ilv_a;
    if (!strcmp(b, "b")) return rd_samples_ilv_b;
    if (!strcmp(b, "m")) return rd_samples_ilv_m;
    return nullptr;
}

// Decode one sample at address A (volume=0, no delta interpolation)
static int16_t decode(const RdSampleEntry* bank, uint32_t A) {
    const RdSampleEntry& se = bank[A];
    bool ag3 = ((A & 0x1C000) != 0) || (((A & 0x2000) != 0) && ((A & 0x1800) != 0));
    uint32_t pa = se.exp | (ag3 ? 1u : 0u);
    uint32_t tmp = pa & 0x3fff;
    int32_t v = rd_samples_exp_table[16384u * (se.exp_sign ? 1u : 0u)
                                     + 1024u * (tmp >> 10)
                                     + (tmp & 1023u)];
    if (se.exp_sign) v -= 0x8000;
    if (v < -32768) v = -32768;
    if (v >  32767) v =  32767;
    return (int16_t)v;
}

// Write minimal RIFF/WAVE mono 16-bit PCM
static void write_wav(const char* path, const int16_t* data, size_t n, uint32_t sr) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return; }
    uint32_t data_size = (uint32_t)(n * 2);
    fwrite("RIFF", 1, 4, f);
    w32(f, 36 + data_size);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    w32(f, 16);
    w16(f, 1);            // PCM
    w16(f, 1);            // mono
    w32(f, sr);           // sample rate
    w32(f, sr * 2);       // byte rate
    w16(f, 2);            // block align
    w16(f, 16);           // bits per sample
    fwrite("data", 1, 4, f);
    w32(f, data_size);
    for (size_t i = 0; i < n; i++) w16(f, (uint16_t)data[i]);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <a|b|m> <wave_high|all> <outdir> [samplerate]\n", argv[0]);
        return 1;
    }
    const char* bank_str = argv[1];
    const char* wave_arg = argv[2];
    const char* outdir   = argv[3];
    uint32_t sr = (argc >= 5) ? (uint32_t)atoi(argv[4]) : 20000;

    const RdSampleEntry* bank = get_bank(bank_str);
    if (!bank) { fprintf(stderr, "Invalid bank: %s\n", bank_str); return 1; }

    int start = 0, end = 64;
    bool all = !strcmp(wave_arg, "all");
    if (!all) {
        start = atoi(wave_arg);
        end = start + 1;
        if (start < 0 || start > 63) {
            fprintf(stderr, "wave_high out of range (0..63)\n");
            return 1;
        }
    }

    for (int wh = start; wh < end; wh++) {
        uint32_t base = (uint32_t)wh << 11;   // region base address
        int16_t buf[2048];
        int min_v = 32767, max_v = -32768;
        double sum_sq = 0.0;
        for (int i = 0; i < 2048; i++) {
            int16_t s = decode(bank, base + (uint32_t)i);
            buf[i] = s;
            int v = s;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
            sum_sq += (double)v * (double)v;
        }
        double rms = sqrt(sum_sq / 2048.0);
        char path[1024];
        snprintf(path, sizeof(path), "%s/wave_%s_%d.wav", outdir, bank_str, wh);
        write_wav(path, buf, 2048, sr);
        printf("Region %d: min=%d max=%d rms=%.2f -> %s\n",
               wh, min_v, max_v, rms, path);
    }
    return 0;
}
