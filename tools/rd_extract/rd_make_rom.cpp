// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// rd_make_rom -- turns a local MKS-20 / MK-80 ROM set into the single blob
// PicoFaceRD embeds. Nothing it produces is in the repository; nothing it
// needs is either.
//
// It links against a checkout of the reference emulator (Michi71/rdpiano or
// giulioz/rdpiano), which is where the descrambling lives. rd_make_rom.sh
// makes a hooked copy of that checkout in a scratch directory so the private
// arrays can be read out -- the emulator itself is never modified and never
// vendored here. Same arrangement as the JV's build_fx_taps.sh.
//
// What comes out is what the firmware actually reads: three packed sample
// banks and the two lookup tables the chip's arithmetic needs. The decoded
// exponent/delta arrays, the parameter ROMs and the program ROM stay on the
// host -- only the emulator ever wanted those.

#include "sound_chip.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static constexpr size_t kBank = 0x20000;   // entries per sample bank

static void read_rom(const std::string& dir, const char* name, u8* buf, size_t len)
{
    const std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "rd_make_rom: cannot open %s\n", path.c_str()); exit(2); }
    const size_t got = fread(buf, 1, len, f);
    fclose(f);
    if (got != len) {
        fprintf(stderr, "rd_make_rom: %s is %zu bytes, expected %zu\n",
                path.c_str(), got, len);
        exit(2);
    }
}

// The firmware reads one 32-bit word per sample: four bytes in one XIP burst
// rather than four scattered reads. Layout is gen_pk4's, unchanged.
static uint32_t pack(uint16_t exp, bool exp_sign, uint16_t delta, bool delta_sign)
{
    return  (static_cast<uint32_t>(exp)   & 0x3FFFu)
         |  (static_cast<uint32_t>(exp_sign   ? 1u : 0u) << 14)
         | ((static_cast<uint32_t>(delta) & 0x01FFu) << 15)
         |  (static_cast<uint32_t>(delta_sign ? 1u : 0u) << 24);
}

struct Model { const char* tag; const char* ic5; const char* ic6; const char* ic7; };

// Which dump is which chip. Established by reproducing the tables this
// instrument shipped until now, byte for byte, for all three models.
static const Model kModels[3] = {
    { "a", "mks20_15179738.BIN", "mks20_15179737.BIN", "mks20_15179736.BIN" },
    { "b", "mks20_15179741.BIN", "mks20_15179740.BIN", "mks20_15179739.BIN" },
    { "m", "MK80_IC5.bin",       "MK80_IC6.bin",       "MK80_IC7.bin"       },
};

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: rd_make_rom <romdir> <out.blob> [ilv.bin]\n");
        return 1;
    }
    const std::string romdir = argv[1];
    const char* outpath = argv[2];
    // Optional third output: the same samples in the 8-byte interleaved form.
    // The firmware has no use for it -- it reads the packed banks -- but the
    // reference emulator the capture drives does, and asking the checkout to
    // carry three megabytes of ROM-derived tables is exactly what this whole
    // arrangement exists to avoid.
    FILE* ilv = (argc == 4) ? fopen(argv[3], "wb") : nullptr;
    if (argc == 4 && !ilv) { fprintf(stderr, "rd_make_rom: cannot write %s\n", argv[3]); return 2; }
    struct IlvEntry { uint16_t exp, delta; uint8_t exp_sign, delta_sign; uint16_t pad; };
    std::vector<IlvEntry> ilv_bank(kBank);

    static u8 ic5[kBank], ic6[kBank], ic7[kBank];
    std::vector<uint32_t> pk4(kBank);

    FILE* out = fopen(outpath, "wb");
    if (!out) { fprintf(stderr, "rd_make_rom: cannot write %s\n", outpath); return 2; }

    SoundChip* chip = nullptr;
    for (const Model& m : kModels) {
        read_rom(romdir, m.ic5, ic5, kBank);
        read_rom(romdir, m.ic6, ic6, kBank);
        read_rom(romdir, m.ic7, ic7, kBank);

        delete chip;
        chip = new SoundChip(ic5, ic6, ic7);

        for (size_t i = 0; i < kBank; i++) {
            pk4[i] = pack(chip->samples_exp[i],   chip->samples_exp_sign[i],
                          chip->samples_delta[i], chip->samples_delta_sign[i]);
        }
        fwrite(pk4.data(), sizeof(uint32_t), kBank, out);
        if (ilv) {
            for (size_t i = 0; i < kBank; i++) {
                ilv_bank[i] = { chip->samples_exp[i], chip->samples_delta[i],
                                (uint8_t)(chip->samples_exp_sign[i] ? 1 : 0),
                                (uint8_t)(chip->samples_delta_sign[i] ? 1 : 0), 0 };
            }
            fwrite(ilv_bank.data(), sizeof(IlvEntry), kBank, ilv);
        }
        printf("  bank %s: %s / %s / %s\n", m.tag, m.ic5, m.ic6, m.ic7);
    }

    // Both tables are computed from the chip's arithmetic, not read from any
    // ROM -- they come out of the emulator's constructor identically whichever
    // set was loaded. They ride in the blob because the firmware needs them and
    // generating them twice would be two places to get it wrong.
    fwrite(chip->phase_exp_table,   sizeof(uint32_t), 0x10000, out);
    fwrite(chip->samples_exp_table, sizeof(uint16_t), 0x8000,  out);
    delete chip;

    const long size = ftell(out);
    fclose(out);
    if (ilv) { printf("  interleaved banks: %ld bytes\n", ftell(ilv)); fclose(ilv); }
    printf("  tables: phase_exp, samples_exp\n");
    printf("rd_make_rom: %ld bytes -> %s\n", size, outpath);
    return 0;
}
