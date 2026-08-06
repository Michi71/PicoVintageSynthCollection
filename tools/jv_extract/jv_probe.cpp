// Differential probe harness for the JV-880 patch structure.
//
// Reads probe specs on stdin, renders each one on the reference emulator and
// writes one CSV feature row per probe. Two things about this are deliberate
// and load-bearing -- see README.md, "Two traps":
//
//   * a FRESH MCU per probe. SC55_Reset() leaves PCM, timer and resampler
//     state behind (and samplesError), which made results order-dependent.
//   * every render is hashed. The comparison that matters is a probe against
//     its control (same byte on a switched-off tone), not against a global
//     baseline: the firmware's parse path takes different amounts of time
//     depending on which byte was touched, and that shift is indistinguishable
//     from a real parameter change in any summary feature.
//
// ROM loading and descrambling are self-contained here on purpose, so the only
// external dependency is the emulator core itself.
#include "mcu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int SR = 32000, PATCH_SZ = 0x16A, NV_PATCH = 0x0D70;
static const int TONE_OFF = 26, TONE_SZ = 84;

// ------------------------------------------------------------------ ROM load

static bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t want) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    out.resize(want);
    size_t got = fread(out.data(), 1, want, f);
    fclose(f);
    if (got != want) { fprintf(stderr, "%s: short read\n", path.c_str()); return false; }
    return true;
}

// Fixed 20-bit address / 8-bit data permutation on the wave ROMs. Only the low
// 20 bits move, so the transform is per 1 MB page.
static void descramble(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    static const int A[20] = {2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19};
    static const int D[8]  = {2, 0, 4, 5, 7, 6, 3, 1};
    uint8_t dmap[256];
    for (int v = 0; v < 256; v++) {
        int d = 0;
        for (int j = 0; j < 8; j++) if (v & (1 << D[j])) d |= 1 << j;
        dmap[v] = (uint8_t)d;
    }
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        size_t a = i & ~(size_t)0xFFFFF;
        for (int j = 0; j < 20; j++) if (i & (1u << j)) a |= (size_t)1 << A[j];
        dst[i] = dmap[src[a]];
    }
}

// --------------------------------------------------------------------- probes

struct Probe { std::string label; int tone, off, val; };

// A raw MIDI message to inject at a given time after note-on. This is what makes
// the modulation matrix reachable: its sources are the mod wheel, aftertouch and
// expression, none of which a bare note-on exercises.
struct MidiEvent { double at; uint8_t b[3]; int len; };

static void render(MCU& m, float* l, float* r, int n) {
    const int CH = 4096;   // the emulator's internal render buffer caps one call
    for (int i = 0; i < n; i += CH)
        m.updateSC55WithSampleRate(l + i, r + i, std::min(CH, n - i), SR);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: jv_probe <romdir> [patchIndexInBankA] [note] [velocity] < probes\n"
                "probe spec: <label> <tone> <offset> <value>\n"
                "  label '#base' applies the change to every probe\n"
                "  offset -1 leaves the patch untouched\n"
                "  a negative tone index addresses patch bytes directly\n");
        return 1;
    }
    std::string dir = argv[1];
    int basePatch = argc > 2 ? atoi(argv[2]) : 0;
    int note = argc > 3 ? atoi(argv[3]) : 60, vel = argc > 4 ? atoi(argv[4]) : 100;

    std::vector<uint8_t> rom1, rom2, nvram, w1raw, w2raw, w1, w2;
    if (!readFile(dir + "/jv880_rom1.bin", rom1, 0x8000) ||
        !readFile(dir + "/jv880_rom2.bin", rom2, 0x40000) ||
        !readFile(dir + "/jv880_nvram.bin", nvram, 0x8000) ||
        !readFile(dir + "/jv880_waverom1.bin", w1raw, 0x200000) ||
        !readFile(dir + "/jv880_waverom2.bin", w2raw, 0x200000))
        return 1;
    descramble(w1raw, w1);
    descramble(w2raw, w2);

    // JV_BANK selects the patch bank: 0 User, 1 A (default), 2 B. The three
    // are not a whole number of patches apart, so an index alone cannot reach
    // bank B -- offsetting into it lands in the rhythm sets instead.
    static const uint32_t kBankOffset[3] = {0x008CE0, 0x010CE0, 0x018CE0};
    const char* envBank = getenv("JV_BANK");
    int bank = envBank ? atoi(envBank) : 1;
    if (bank < 0 || bank > 2) bank = 1;
    uint8_t base[PATCH_SZ];
    memcpy(base, rom2.data() + kBankOffset[bank] + basePatch * PATCH_SZ, PATCH_SZ);

    std::vector<Probe> probes, baseMods;
    std::vector<MidiEvent> midi;
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        Probe p; char lab[128];
        // "#rom <offset> <value>" patches rom2 itself before the MCU starts, so
        // the ROM tables (sample table, multisample table) can be probed the
        // same way patch bytes are.
        unsigned ro, rv;
        if (sscanf(line, "#rom %i %i", &ro, &rv) == 2) {
            if (ro < rom2.size()) rom2[ro] = (uint8_t)rv;
            continue;
        }
        // "#midi <seconds after note-on> <status> <d1> [d2]"
        MidiEvent me{}; double at; unsigned s0, d1, d2 = 0xFFFF;
        int n = sscanf(line, "#midi %lf %i %i %i", &at, &s0, &d1, &d2);
        if (n >= 3) {
            me.at = at; me.b[0] = (uint8_t)s0; me.b[1] = (uint8_t)d1;
            me.len = 2;
            if (n == 4) { me.b[2] = (uint8_t)d2; me.len = 3; }
            midi.push_back(me);
            continue;
        }
        if (sscanf(line, "%127s %d %d %d", lab, &p.tone, &p.off, &p.val) == 4) {
            p.label = lab;
            (p.label == "#base" ? baseMods : probes).push_back(p);
        }
    }
    for (auto& b : baseMods)
        base[b.tone >= 0 ? TONE_OFF + b.tone * TONE_SZ + b.off : b.off] = (uint8_t)b.val;
    std::sort(midi.begin(), midi.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.at < b.at; });

    // Calibration needs long renders (release can run for seconds) and the raw
    // waveform, not just summary features.
    const char* envHold = getenv("JV_HOLD");
    const char* envRel  = getenv("JV_REL");
    const char* dumpDir = getenv("JV_DUMP");
    // JV_WARM shifts the note-on in time without changing anything else, which
    // is how a free-running LFO is told apart from a key-synced one.
    const char* envWarm = getenv("JV_WARM");
    const int nWarm = (int)(SR * (envWarm ? atof(envWarm) : 1.0));
    const int nHold = (int)(SR * (envHold ? atof(envHold) : 1.5));
    const int nRel  = (int)(SR * (envRel ? atof(envRel) : 1.0));
    std::vector<float> W(nWarm), W2(nWarm), L(nHold + nRel), R(nHold + nRel), m(nHold + nRel);
    fprintf(stderr, "hold=%.2fs rel=%.2fs%s\n", (double)nHold / SR, (double)nRel / SR,
            dumpDir ? " (dumping)" : "");

    printf("label,hash,peak,rms,t90,b0,b1,b2,b3,b4,b5,b6,b7,bright,pitch,balance,rel\n");
    for (auto& p : probes) {
        MCU* mcu = new MCU();
        mcu->startSC55(rom1.data(), rom2.data(), w1.data(), w2.data(), nvram.data());

        uint8_t patch[PATCH_SZ];
        memcpy(patch, base, PATCH_SZ);
        if (p.off >= 0)
            patch[p.tone >= 0 ? TONE_OFF + p.tone * TONE_SZ + p.off : p.off] = (uint8_t)p.val;
        memcpy(mcu->nvram + NV_PATCH, patch, PATCH_SZ);
        mcu->nvram[0x11] = 1;              // patch mode (as opposed to rhythm)
        mcu->SC55_Reset();
        render(*mcu, W.data(), W2.data(), nWarm);   // let the firmware boot

        uint8_t on[3]  = {0x90, (uint8_t)note, (uint8_t)vel};
        uint8_t off[3] = {0x80, (uint8_t)note, 0x40};
        mcu->postMidiSC55(on, 3);
        // render the hold in segments so queued controllers land on time
        int done = 0;
        for (const MidiEvent& m : midi) {
            int at = (int)(m.at * SR);
            if (at > nHold) at = nHold;
            if (at > done) { render(*mcu, L.data() + done, R.data() + done, at - done); done = at; }
            mcu->postMidiSC55(m.b, m.len);
        }
        if (done < nHold) render(*mcu, L.data() + done, R.data() + done, nHold - done);
        mcu->postMidiSC55(off, 3);
        render(*mcu, L.data() + nHold, R.data() + nHold, nRel);
        delete mcu;

        const int N = nHold, T = nHold + nRel;
        for (int i = 0; i < T; i++) m[i] = 0.5f * (L[i] + R[i]);

        uint64_t h = 1469598103934665603ull;   // FNV-1a over the whole render
        const unsigned char* q = (const unsigned char*)m.data();
        for (size_t i = 0; i < (size_t)T * sizeof(float); i++)
            h = (h ^ q[i]) * 1099511628211ull;

        if (dumpDir) {   // interleaved stereo float32; note-off is at frame nHold
            std::string path = std::string(dumpDir) + "/" + p.label + ".f32";
            if (FILE* df = fopen(path.c_str(), "wb")) {
                std::vector<float> il((size_t)T * 2);
                for (int i = 0; i < T; i++) { il[2 * i] = L[i]; il[2 * i + 1] = R[i]; }
                fwrite(il.data(), sizeof(float), il.size(), df);
                fclose(df);
            } else {
                fprintf(stderr, "cannot write %s\n", path.c_str());
            }
        }

        double peak = 0;
        for (int i = 0; i < N; i++) peak = std::max(peak, (double)fabs(m[i]));
        double e = 0, d = 0, rl = 0, rr = 0;
        for (int i = 0; i < N; i++) {
            e += m[i] * m[i];
            if (i) { double dd = m[i] - m[i - 1]; d += dd * dd; }
            rl += L[i] * L[i];
            rr += R[i] * R[i];
        }
        double rms = sqrt(e / N);
        int t90 = 0;                            // time to 90 % of peak -> attack
        for (int i = 0; i < N; i++) if (fabs(m[i]) >= 0.9 * peak) { t90 = i; break; }
        double bands[8];
        for (int k = 0; k < 8; k++) {
            double s = 0; int a = k * N / 8, b = (k + 1) * N / 8;
            for (int i = a; i < b; i++) s += m[i] * m[i];
            bands[k] = sqrt(s / (b - a));
        }
        double er = 0;
        for (int i = N; i < T; i++) er += m[i] * m[i];
        double relv = rms > 1e-12 ? sqrt(er / nRel) / rms : 0;
        int w0 = N / 2, wl = 4096, bl = 0; double bc = 0, nn = 0;
        for (int i = 0; i < wl; i++) nn += m[w0 + i] * m[w0 + i];
        if (nn > 1e-16)
            for (int lag = 20; lag < 1200; lag++) {
                double c = 0;
                for (int i = 0; i < wl; i++) c += m[w0 + i] * m[w0 + i + lag];
                if (c > bc) { bc = c; bl = lag; }
            }

        printf("%s,%016llx,%.6f,%.6f,%d", p.label.c_str(), (unsigned long long)h, peak, rms, t90);
        for (int k = 0; k < 8; k++) printf(",%.6f", bands[k]);
        printf(",%.4f,%.2f,%.4f,%.4f\n", e > 1e-20 ? d / e : 0, bl ? (double)SR / bl : 0,
               (rl + rr) > 1e-20 ? rl / (rl + rr) : 0.5, relv);
        fflush(stdout);
    }
    return 0;
}
