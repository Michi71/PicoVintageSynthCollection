// gen_pk4.cpp -- Host-Tool (C++17): packt RdSampleEntry (8 Byte) in uint32_t (4 Byte)
#include "rom_tables.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

static constexpr size_t N = 0x20000;

static uint32_t pack_entry(const RdSampleEntry& e) {
    // Layout: bits[13:0]=exp, bit[14]=exp_sign, bits[23:15]=delta&0x1FF, bit[24]=delta_sign
    uint32_t pk = (static_cast<uint32_t>(e.exp)        & 0x3FFFu)
                | (static_cast<uint32_t>(e.exp_sign ? 1u : 0u) << 14)
                | ((static_cast<uint32_t>(e.delta)    & 0x1FFu)  << 15)
                | (static_cast<uint32_t>(e.delta_sign ? 1u : 0u) << 24);
    return pk;
}

static void verify_bank(const RdSampleEntry* bank, const char* name) {
    for (size_t i = 0; i < N; ++i) {
        const RdSampleEntry& e = bank[i];
        if (e.exp > 0x3FFFu) {
            std::fprintf(stderr, "verify error (%s): idx=%zu exp=0x%04X > 0x3FFF\n",
                         name, i, static_cast<unsigned>(e.exp));
            std::exit(1);
        }
        if (e.delta > 0x1FFu) {
            std::fprintf(stderr, "verify error (%s): idx=%zu delta=0x%04X > 0x1FF\n",
                         name, i, static_cast<unsigned>(e.delta));
            std::exit(1);
        }
    }
}

static void emit_bank(const RdSampleEntry* bank, char suffix) {
    char path[128];
    // Relative to the repository root, which is where this tool is meant to be
    // run from (the engine moved under instruments/ with the monorepo merge).
    std::snprintf(path, sizeof(path), "instruments/PicoFaceRD/src/rd_engine/rd_samples_pk4_%c.cpp", suffix);

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }

    static char fbuf[1u << 20];
    std::setvbuf(f, fbuf, _IOFBF, sizeof(fbuf));

    std::fprintf(f,
        "#include \"rom_tables.h\"\n"
        "#if defined(TARGET_RP2350) || defined(PICO_PLATFORM)\n"
        "#define PK4_ATTR __attribute__((aligned(8), section(\".rodata\")))\n"
        "#else\n"
        "#define PK4_ATTR\n"
        "#endif\n"
        "const uint32_t rd_samples_pk4_%c[0x20000] PK4_ATTR = {\n",
        suffix);

    for (size_t i = 0; i < N; ++i) {
        uint32_t v = pack_entry(bank[i]);
        if ((i & 7u) == 0u) std::fputc(' ', f);
        std::fprintf(f, "0x%08X", v);
        bool last = (i + 1 == N);
        if (!last) {
            std::fputc(',', f);
            std::fputc((i & 7u) == 7u ? '\n' : ' ', f);
        } else {
            std::fputc('\n', f);
        }
    }
    std::fprintf(f, "};\n");
    std::fclose(f);
    std::printf("done: %s\n", path);
}

int main() {
    verify_bank(rd_samples_ilv_a, "a");
    verify_bank(rd_samples_ilv_b, "b");
    verify_bank(rd_samples_ilv_m, "m");
    std::printf("verification ok\n");
    emit_bank(rd_samples_ilv_a, 'a');
    emit_bank(rd_samples_ilv_b, 'b');
    emit_bank(rd_samples_ilv_m, 'm');
    return 0;
}
