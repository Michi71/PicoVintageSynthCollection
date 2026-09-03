// Render one note of a factory patch through the D5 engine, stereo f32 at
// 32 kHz, for the VST comparison in tools/d5_vst. Needs the ROM build
// (d5_patch_data.h and d5_pcm.bin from the configured build directory).
//
//   render_note <d5_pcm.bin> <out.f32> <patch index> <note> <velocity 1..127>
//               <hold s> <tail s> [dry]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include "d5_engine/d5_patch.h"
#include "d5_engine/d5_patch_map.h"
#include "d5_patch_data.h"

static std::vector<int16_t> load_blob(const char* p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<int16_t> v;
    int16_t w;
    while (f.read(reinterpret_cast<char*>(&w), 2)) v.push_back(w);
    return v;
}

int main(int argc, char** argv) {
    // "render_note dumpbank out.bin" writes the 384 x 448 patch bytes the
    // engine plays, so the VST side gets exactly the same bytes.
    if (argc == 3 && std::strcmp(argv[1], "dumpbank") == 0) {
        std::ofstream f(argv[2], std::ios::binary);
        for (int i = 0; i < d5::kPatchCount; ++i)
            f.write(reinterpret_cast<const char*>(d5::kPatchData[i]), 448);
        std::FILE* n = std::fopen((std::string(argv[2]) + ".names").c_str(), "w");
        for (int i = 0; i < d5::kPatchCount; ++i) std::fprintf(n, "%d\t%s\n", i, d5::kPatchNames[i]);
        std::fclose(n);
        return 0;
    }
    if (argc < 8) {
        std::fprintf(stderr, "usage: render_note pcm.bin out.f32 idx note vel hold tail [dry]\n"
                             "       render_note dumpbank out.bin\n");
        return 2;
    }
    const float kSR = 32000.0f;
    auto blob = load_blob(argv[1]);
    const int idx = std::atoi(argv[3]);
    const int note = std::atoi(argv[4]);
    const float vel = std::atof(argv[5]) / 127.0f;   // the engine takes 0..1
    const float hold = std::atof(argv[6]);
    const float tail = std::atof(argv[7]);
    const bool dry = argc > 8 && std::strcmp(argv[8], "dry") == 0;
    if (idx < 0 || idx >= d5::kPatchCount) return 2;
    d5::PatchSpec s = d5::patch_from_bytes(d5::kPatchData[idx], blob.data());
    if (dry) {
        s.reverb.balance = 0.0f;
        s.upper.chorus.balance = 0.0f;
        s.lower.chorus.balance = 0.0f;
    }
    d5::Patch p;
    p.configure(s, kSR);
    p.note_on(note, vel);
    std::vector<float> o;
    o.reserve(static_cast<size_t>(kSR * (hold + tail)) * 2);
    for (int i = 0; i < static_cast<int>(kSR * hold); ++i) {
        float l, r;
        p.next_stereo(l, r);
        o.push_back(l);
        o.push_back(r);
    }
    p.note_off(note);
    for (int i = 0; i < static_cast<int>(kSR * tail); ++i) {
        float l, r;
        p.next_stereo(l, r);
        o.push_back(l);
        o.push_back(r);
    }
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()), o.size() * sizeof(float));
    return 0;
}
