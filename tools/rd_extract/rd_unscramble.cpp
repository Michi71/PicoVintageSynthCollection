// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// rd_unscramble -- writes out a ROM set's program and parameter ROMs with the
// address and data lines put back in order, which is what reading the firmware
// needs. Same arrangement as rd_make_rom: it links against a hooked copy of a
// reference-emulator checkout, because that is where the descrambling lives,
// and nothing of that emulator is in this repository.
//
// The sample ROMs are not written here -- rd_make_rom is for those.

#include "mcu.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static void read_rom(const std::string& dir, const char* name, u8* buf, size_t len)
{
    const std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "rd_unscramble: cannot open %s\n", path.c_str()); exit(2); }
    const size_t got = fread(buf, 1, len, f);
    fclose(f);
    if (got != len) {
        fprintf(stderr, "rd_unscramble: %s is %zu bytes, expected %zu\n",
                path.c_str(), got, len);
        exit(2);
    }
}

static void write_out(const char* path, const void* p, size_t n)
{
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "rd_unscramble: cannot write %s\n", path); exit(2); }
    fwrite(p, 1, n, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    if (argc != 7) {
        fprintf(stderr,
            "usage: rd_unscramble <romdir> <progrom> <paramsrom> <ic5> "
            "<out-program.bin> <out-params.bin>\n");
        return 1;
    }
    const std::string dir = argv[1];
    static u8 prog[0x2000], prm[0x20000], ic[0x20000];
    read_rom(dir, argv[2], prog, sizeof prog);
    read_rom(dir, argv[3], prm,  sizeof prm);
    // The Mcu wants three sample images to construct its sound chip. Only the
    // descrambling is wanted here, so one image serves for all three.
    read_rom(dir, argv[4], ic, sizeof ic);

    Mcu* m = new Mcu(ic, ic, ic, prog, prm);
    m->loadSounds(ic, ic, ic, prm, 0);
    write_out(argv[5], m->program_rom,    0x2000);
    write_out(argv[6], m->params_rom_tmp, 0x20000);
    printf("rd_unscramble: %s -> %s, %s -> %s\n", argv[2], argv[5], argv[3], argv[6]);
    return 0;
}
