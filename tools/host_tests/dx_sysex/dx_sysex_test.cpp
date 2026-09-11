// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// dx_sysex_test.cpp -- PicoFaceDX SysEx round trip, both directions.
//
// Written while chasing a report that a voice synced through Soundmondo came
// back with a wrong operator 4. It did not: the wire was byte-exact and the
// editor's display was at fault. This test is what settles that class of
// question in a second instead of over several rounds of guessing.
//
// Round trip through the real src/midi_reface.cpp and the shared layer it
// sits on (core/src/reface/reface_midi.cpp), both directions:
//
//   TX: put a known patch in the engine, ask for a voice dump, parse what came
//       out and compare byte for byte.
//   RX: encode a known patch as an editor would, feed it in block by block,
//       apply, and compare byte for byte.
//
// The patch is filled with a distinct value per byte, so any swap, shift or
// dropped block shows up as a named field rather than as a plausible number.
#include "midi_reface.h"
#include "DX_Synth_Bridge.h"
#include "dx_patch_stage.h"
#include "ipc.h"
#include "midi_output_usb.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <vector>

// --- stub bodies -----------------------------------------------------------
static std::vector<uint8_t> g_out;
bool MIDIOutStub::write(const uint8_t* d, uint16_t n) { g_out.insert(g_out.end(), d, d + n); return true; }
MIDIOutStub& usbMidiOut() { static MIDIOutStub s; return s; }
bool tud_midi_mounted(void) { return true; }
absolute_time_t get_absolute_time(void) { return 0; }
uint32_t to_ms_since_boot(absolute_time_t) { return 0; }
void preset_set_current(uint8_t) {}
void preset_stage(uint8_t) {}
extern "C" int ui_get_octave(void) { return 0; }

// --- helpers ---------------------------------------------------------------
static const char* fieldName(int byteIndex)
{
    static char buf[48];
    if (byteIndex < 38) {
        static const char* c[] = {
            "name0","name1","name2","name3","name4","name5","name6","name7","name8","name9",
            "res1a","res1b","transpose","monoPoly","portaTime","pbRange","algorithm","lfoWave",
            "lfoSpeed","lfoDelay","lfoPMD","pegRate1","pegRate2","pegRate3","pegRate4",
            "pegLevel1","pegLevel2","pegLevel3","pegLevel4","fx1type","fx1p1","fx1p2",
            "fx2type","fx2p1","fx2p2","res2a","res2b","res2c"};
        snprintf(buf, sizeof(buf), "common.%s", c[byteIndex]);
        return buf;
    }
    static const char* o[] = {
        "enable","egRate1","egRate2","egRate3","egRate4","egLevel1","egLevel2","egLevel3","egLevel4",
        "rateScaling","scaleLD","scaleRD","scaleLC","scaleRC","lfoAMD","lfoPMDEnable","pegEnable",
        "velSens","outLevel","feedback","fbType","freqMode","freqCoarse","freqFine","freqDetune",
        "res0","res1","res2"};
    const int n = byteIndex - 38;
    snprintf(buf, sizeof(buf), "op%d.%s", n / 28 + 1, o[n % 28]);
    return buf;
}

static RDX_Patch patternPatch(uint8_t seed)
{
    RDX_Patch p{};
    uint8_t* b = reinterpret_cast<uint8_t*>(&p);
    for (size_t i = 0; i < sizeof(RDX_Patch); ++i) b[i] = (uint8_t)((i * 7 + seed) % 127) + 1;
    return p;
}

static int failures = 0;
static void compare(const char* what, const RDX_Patch& got, const RDX_Patch& want)
{
    const uint8_t* g = reinterpret_cast<const uint8_t*>(&got);
    const uint8_t* w = reinterpret_cast<const uint8_t*>(&want);
    int bad = 0;
    for (size_t i = 0; i < sizeof(RDX_Patch); ++i) {
        if (g[i] != w[i]) {
            if (bad < 12) printf("    %-22s want %3d  got %3d\n", fieldName((int)i), w[i], g[i]);
            bad++;
        }
    }
    printf("  %-46s %s\n", what, bad ? "FAIL" : "pass");
    if (bad) { printf("    (%d of %zu bytes differ)\n", bad, sizeof(RDX_Patch)); failures++; }
}

// Parse a stream of bulk blocks back into a patch, the way an editor would.
static bool parseStream(const std::vector<uint8_t>& s, RDX_Patch& out, int& blocks)
{
    blocks = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != 0xF0) return false;
        size_t end = i;
        while (end < s.size() && s[end] != 0xF7) end++;
        if (end >= s.size()) return false;
        const uint8_t* d = &s[i];
        const uint16_t len = (uint16_t)(end - i + 1);

        const uint16_t bc = (uint16_t)(((d[5] & 0x7F) << 7) | (d[6] & 0x7F));
        if (len != (uint16_t)(bc + 9)) return false;
        uint32_t sum = 0;
        for (uint16_t k = 7; k < (uint16_t)(len - 1); k++) sum += d[k];
        if (sum & 0x7F) { printf("    checksum error in block %d\n", blocks); return false; }

        const uint8_t ah = d[8], am = d[9];
        const uint16_t dlen = (uint16_t)(bc - 4);
        const uint8_t* data = &d[11];
        if (ah == 0x30 && dlen == 38)      memcpy(&out.common, data, 38);
        else if (ah == 0x31 && am < 4 && dlen == 28) memcpy(&out.ops[am], data, 28);
        blocks++;
        i = end + 1;
    }
    return true;
}

// Build the seven blocks an editor sends for a full voice.
static std::vector<uint8_t> encodeVoice(const RDX_Patch& p)
{
    std::vector<uint8_t> s;
    auto block = [&](uint8_t ah, uint8_t am, const uint8_t* data, uint8_t len) {
        const uint16_t bc = (uint16_t)(4 + len);
        std::vector<uint8_t> m = {0xF0, 0x43, 0x00, 0x7F, 0x1C,
                                  (uint8_t)((bc >> 7) & 0x7F), (uint8_t)(bc & 0x7F),
                                  0x05, ah, am, 0x00};
        uint32_t sum = 0x05 + ah + am + 0x00;
        for (uint8_t k = 0; k < len; k++) { m.push_back(data[k] & 0x7F); sum += data[k] & 0x7F; }
        m.push_back((uint8_t)((0x80 - (sum & 0x7F)) & 0x7F));
        m.push_back(0xF7);
        s.insert(s.end(), m.begin(), m.end());
    };
    block(0x0E, 0x0F, nullptr, 0);
    block(0x30, 0x00, reinterpret_cast<const uint8_t*>(&p.common), 38);
    for (uint8_t o = 0; o < 4; o++)
        block(0x31, o, reinterpret_cast<const uint8_t*>(&p.ops[o]), 28);
    block(0x0F, 0x0F, nullptr, 0);
    return s;
}

int main()
{
    DX_Synth_Bridge dx;
    RefaceMidi rm;
    rm.init(&dx);

    // ---------- TX: device -> editor ----------
    printf("TX: full voice dump requested by an editor\n");
    const RDX_Patch live = patternPatch(0);
    dx.patch() = live;
    g_out.clear();

    const uint8_t req[] = {0xF0, 0x43, 0x20, 0x7F, 0x1C, 0x05, 0x0E, 0x0F, 0x00, 0xF7};
    rm.onSysEx(req, sizeof(req));

    printf("  emitted %zu bytes\n", g_out.size());
    RDX_Patch received{};
    int blocks = 0;
    if (!parseStream(g_out, received, blocks)) { printf("  stream malformed  FAIL\n"); failures++; }
    else {
        printf("  parsed %d blocks (expect 7)\n", blocks);
        if (blocks != 7) { printf("  block count  FAIL\n"); failures++; }
        compare("patch read back equals the live patch", received, live);
    }

    // ---------- RX: editor -> device ----------
    printf("\nRX: full voice sent by an editor\n");
    const RDX_Patch sent = patternPatch(63);
    const auto stream = encodeVoice(sent);
    printf("  feeding %zu bytes in 7 messages\n", stream.size());

    size_t i = 0;
    while (i < stream.size()) {
        size_t end = i;
        while (end < stream.size() && stream[end] != 0xF7) end++;
        rm.onSysEx(&stream[i], (uint16_t)(end - i + 1));
        i = end + 1;
    }

    // Drain the ring the way DX_Instrument::applyIpc does for PATCH_APPLY.
    bool applied = false;
    uint32_t pkt;
    while (dx_ipc_pop(&pkt))
        if (ipc_type(pkt) == IPC_CMD_DX_PATCH_APPLY) { dx.patch() = dx_patch_stage(); applied = true; }
    printf("  patch apply reached the engine: %s\n", applied ? "yes" : "NO");
    if (!applied) failures++;
    compare("engine patch equals what was sent", dx.patch(), sent);

    // ---------- the shared reface layer (core/src/reface/reface_midi.cpp) ----------
    // The DX is the one port with a host test, so the SYSTEM block, the
    // framing and the channel handling that CP and YC share with it are
    // pinned here.
    printf("\nSYSTEM block and channel handling (shared layer)\n");
    {
        auto check = [&](bool ok, const char* what) {
            printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
            if (!ok) failures++;
        };
        auto drain = [&]() { uint32_t p; while (dx_ipc_pop(&p)) {} };

        // Identity Request is answered with the DX's identity, byte for byte.
        g_out.clear();
        const uint8_t idreq[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
        rm.onSysEx(idreq, sizeof(idreq));
        const uint8_t idreply[15] = {0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x43, 0x00, 0x41, 0x53, 0x06,
                                     0x03, 0x00, 0x00, 0x7F, 0xF7};
        check(g_out.size() == 15 && memcmp(g_out.data(), idreply, 15) == 0, "identity reply");

        // Master tune as the data list sends it: one Parameter Change, four
        // nibbles, 0x0410 = +1.6 cents. It reaches the producer as the raw value.
        drain();
        const uint8_t tune[] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x05, 0x00, 0x00, 0x02, 0x00, 0x04, 0x01, 0x00, 0xF7};
        rm.onSysEx(tune, sizeof(tune));
        bool tuneSeen = false; uint16_t raw = 0; uint32_t pkt2;
        while (dx_ipc_pop(&pkt2)) if (ipc_type(pkt2) == IPC_CMD_DX_MASTER_TUNE) { tuneSeen = true; raw = ipc_d2(pkt2); }
        check(tuneSeen && raw == 0x0410, "four-nibble tune write reaches the engine as 0x0410");

        // A SYSTEM dump request returns the 32-byte block with that tune in it.
        g_out.clear();
        const uint8_t sysreq[] = {0xF0, 0x43, 0x20, 0x7F, 0x1C, 0x05, 0x00, 0x00, 0x00, 0xF7};
        rm.onSysEx(sysreq, sizeof(sysreq));
        // F0 43 00 7F 1C bh bl 05 00 00 00 <32 bytes> sum F7 = 45 bytes
        bool sysOk = g_out.size() == 45 && g_out[5] == 0 && g_out[6] == 36 && g_out[7] == 0x05
                     && g_out[11 + 2] == 0x00 && g_out[11 + 3] == 0x04 && g_out[11 + 4] == 0x01 && g_out[11 + 5] == 0x00
                     && g_out[11 + 1] == 0x10 /* receive omni */ && g_out[44] == 0xF7;
        uint32_t sum = 0;
        for (size_t k = 7; sysOk && k + 1 < g_out.size(); ++k) sum += g_out[k];
        check(sysOk && (sum & 0x7F) == 0, "SYSTEM dump: 36-byte count, tune nibbles, checksum");

        // A bulk block with a bad checksum is dropped: the tune stays.
        std::vector<uint8_t> bad(g_out);
        bad[11 + 2] = 0x07;                      // change a tune nibble, leave the checksum
        drain();
        rm.onSysEx(bad.data(), (uint16_t) bad.size());
        tuneSeen = false;
        while (dx_ipc_pop(&pkt2)) if (ipc_type(pkt2) == IPC_CMD_DX_MASTER_TUNE) tuneSeen = true;
        check(!tuneSeen, "bulk block with a bad checksum is ignored");

        // Omni off (CC 124) narrows the receive channel to 1, omni on (125) opens it.
        rm.onControlChange(124, 0, 0);
        check(rm.getRxChannel() == 0, "CC 124 sets receive channel 1");
        rm.onNoteOn(60, 100, 3);                 // channel 4: filtered
        drain();
        rm.onControlChange(125, 0, 0);
        check(rm.getRxChannel() == RefaceMidi::RX_CH_ALL, "CC 125 restores omni");

        // A note-on with velocity 0 is a note-off (YC used to keep its own
        // USB parser for exactly this).
        drain();
        rm.onNoteOn(60, 0, 0);
        bool offSeen = false, onSeen = false;
        while (dx_ipc_pop(&pkt2)) {
            if (ipc_type(pkt2) == IPC_CMD_DX_NOTE_OFF) offSeen = true;
            if (ipc_type(pkt2) == IPC_CMD_DX_NOTE_ON) onSeen = true;
        }
        check(offSeen && !onSeen, "note-on with velocity 0 is a note-off");

        // A record from a build that seeded the tune with zeros is read as centred.
        uint8_t blk[32] = {0};
        blk[1] = 0x10;                           // omni, as a real record would have it
        drain();
        rm.loadSystemBlock(blk);
        raw = 0;
        while (dx_ipc_pop(&pkt2)) if (ipc_type(pkt2) == IPC_CMD_DX_MASTER_TUNE) raw = ipc_d2(pkt2);
        check(raw == 0x0400, "zero tune in an old settings record loads as the centre 0x0400");
    }

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
