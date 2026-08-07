// Host check for the cut-down wave blob PICOFACEJV_4MB builds.
//
// jv_make_blob.py --banks relocates the samples those banks reach into a
// shorter blob and rewrites the sample table to match. Nothing is resampled or
// requantised, so the claim is a strong one: every patch of every kept bank has
// to render bit-identically to the same patch out of the full ROM. This checks
// exactly that, sample by sample, and it is the check the instrument README
// points at.
//
//   python3 tools/jv_extract/jv_make_blob.py <romdir> /tmp/jvlite --banks=A,B
//   c++ -O2 -std=c++17 -Iinstruments/PicoFaceJV/include -Itools/jv_extract \
//       -o jv_blob_test tools/host_tests/jv_blob_test/jv_blob_test.cpp \
//       instruments/PicoFaceJV/src/jv_engine/jv_engine.cpp -lm
//   ./jv_blob_test <romdir> /tmp/jvlite A B
//
// The descrambled wave copies jv_rom.py caches in <romdir>/Cache are used when
// they exist, so this test cannot disagree with the generator about the
// permutation -- getting that wrong once cost an afternoon of chasing a
// segfault that was really a silent mismatch.
#include "jv_engine/jv_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int SR = 32000;
static const int ADDR_PERM[20] = {2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19};
static const int DATA_PERM[8]  = {2, 0, 4, 5, 7, 6, 3, 1};

static bool slurp(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    const bool ok = n >= 0 && fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

// The address permutation is 20 bits wide, so it applies per 1 MB page.
static void descramble(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    out.assign(in.size(), 0);
    for (size_t page = 0; page < in.size(); page += 1u << 20) {
        for (uint32_t a = 0; a < (1u << 20); ++a) {
            uint32_t na = 0;
            for (int i = 0; i < 20; ++i) if (a & (1u << ADDR_PERM[i])) na |= 1u << i;
            uint8_t d = in[page + a], nd = 0;
            for (int i = 0; i < 8; ++i) if (d & (1u << DATA_PERM[i])) nd |= 1u << i;
            out[page + na] = nd;
        }
    }
}

static bool loadWave(const std::string& romdir, std::vector<uint8_t>& wave) {
    wave.clear();
    for (const char* name : {"jv880_waverom1.bin", "jv880_waverom2.bin"}) {
        std::vector<uint8_t> raw, done;
        if (slurp(romdir + "/Cache/" + name, done) && done.size() == 0x200000) {
            wave.insert(wave.end(), done.begin(), done.end());
            continue;
        }
        if (!slurp(romdir + "/" + name, raw) || raw.size() != 0x200000) {
            fprintf(stderr, "cannot read %s\n", name);
            return false;
        }
        descramble(raw, done);
        wave.insert(wave.end(), done.begin(), done.end());
    }
    return true;
}

// A chord rather than a single note: layered patches allocate several tones and
// a wrong relocation in any one of them shows up, where one note might only
// reach the zone that happened to be placed correctly.
static void render(const jv::RomView& rom, int bank, int patch,
                   std::vector<float>& L, std::vector<float>& R) {
    jv::Engine e;
    if (!e.init(rom, SR)) { fprintf(stderr, "engine init refused the ROM\n"); exit(1); }
    e.selectPatch(bank, patch);
    e.noteOn(48, 110);
    e.noteOn(60, 100);
    e.noteOn(67, 64);
    L.assign(SR, 0.0f);
    R.assign(SR, 0.0f);
    for (int i = 0; i < SR; i += 64) e.render(&L[i], &R[i], 64);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <romdir> <blobdir> <bank> [bank...]   (bank = User|A|B)\n",
                argv[0]);
        return 2;
    }
    const std::string romdir = argv[1], blobdir = argv[2];

    std::vector<uint8_t> rom2, wave, cutRom2, cutWave;
    if (!slurp(romdir + "/jv880_rom2.bin", rom2) || rom2.size() != 0x40000) {
        fprintf(stderr, "cannot read jv880_rom2.bin\n");
        return 1;
    }
    if (!loadWave(romdir, wave)) return 1;
    if (!slurp(blobdir + "/jv_rom2.bin", cutRom2) || !slurp(blobdir + "/jv_wave.bin", cutWave)) {
        fprintf(stderr, "cannot read the generated blob in %s\n", blobdir.c_str());
        return 1;
    }

    const jv::RomView full{wave.data(), wave.size(), rom2.data(), rom2.size()};
    const jv::RomView cut{cutWave.data(), cutWave.size(), cutRom2.data(), cutRom2.size()};
    printf("full %.2f MB wave, cut %.2f MB (%.0f%%)\n",
           wave.size() / 1048576.0, cutWave.size() / 1048576.0,
           100.0 * cutWave.size() / wave.size());

    int bad = 0, silent = 0, checked = 0;
    std::vector<float> aL, aR, bL, bR;
    for (int i = 3; i < argc; ++i) {
        const std::string name = argv[i];
        const int bank = (name == "User") ? 0 : (name == "A") ? 1 : (name == "B") ? 2 : -1;
        if (bank < 0) { fprintf(stderr, "unknown bank %s\n", name.c_str()); return 2; }
        for (int p = 0; p < 64; ++p) {
            render(full, bank, p, aL, aR);
            render(cut, bank, p, bL, bR);
            double diff = 0.0, energy = 0.0;
            for (size_t k = 0; k < aL.size(); ++k) {
                diff = std::fmax(diff, std::fabs(aL[k] - bL[k]));
                diff = std::fmax(diff, std::fabs(aR[k] - bR[k]));
                energy += (double)aL[k] * aL[k] + (double)aR[k] * aR[k];
            }
            ++checked;
            // A patch that is silent in both would pass trivially, so it is
            // counted and reported rather than quietly accepted.
            if (energy < 1e-9) ++silent;
            if (diff > 0.0) {
                printf("  DIFF %s %02d: max |d| = %g\n", name.c_str(), p + 1, diff);
                ++bad;
            }
        }
    }
    printf("%d/%d patches differ; %d silent in both\n", bad, checked, silent);
    return bad ? 1 : 0;
}
