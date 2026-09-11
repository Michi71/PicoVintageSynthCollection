// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
//
// yc_sysex_test.cpp -- PicoFaceYC's MIDI layer, from the wire into the engine.
//
// Runs the real src/midi_reface.cpp on the shared reface layer
// (core/src/reface/reface_midi.cpp) and drains the same-core ring through the
// same yc_ipc_apply() the firmware uses, so what lands in yc_engine_state_t
// here is what lands there.
//
// What is pinned: the identity reply and model byte from the Yamaha reface
// Data List (PO-B0), the TG bulk dump an editor gets for a Dump Request on
// the bulk header address - three blocks with byte count and checksum -, a
// bulk dump sent by an editor reaching every parameter, Parameter Change and
// Request on the TG block with the data list's value ranges enforced, and the
// controllers the YC recognises without MIDI Control: pitch bend, mod wheel
// (rotary speed), channel volume, expression, sustain, reset all controllers,
// all sound/notes off.
#include "midi_reface.h"
#include "YC_Synth_Bridge.h"
#include "yc_ipc_apply.h"
#include "midi_output_usb.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// --- stub bodies -----------------------------------------------------------
static std::vector<uint8_t> g_out;
bool MIDIOutStub::write(const uint8_t* d, uint16_t n) { g_out.insert(g_out.end(), d, d + n); return true; }
MIDIOutStub& usbMidiOut() { static MIDIOutStub s; return s; }
bool tud_midi_mounted(void) { return true; }
absolute_time_t get_absolute_time(void) { return 0; }
uint32_t to_ms_since_boot(absolute_time_t) { return 0; }

// --- helpers ---------------------------------------------------------------
static int failures = 0;
static void check(bool ok, const char* what)
{
    printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) failures++;
}

static void hex(const std::vector<uint8_t>& v)
{
    for (uint8_t b : v) printf(" %02X", b);
    printf("\n");
}

// Split the transmitted stream into complete exclusive messages.
static std::vector<std::vector<uint8_t>> messages(const std::vector<uint8_t>& s)
{
    std::vector<std::vector<uint8_t>> out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != 0xF0) { i++; continue; }
        size_t end = i;
        while (end < s.size() && s[end] != 0xF7) end++;
        if (end >= s.size()) break;
        out.emplace_back(s.begin() + (long) i, s.begin() + (long) end + 1);
        i = end + 1;
    }
    return out;
}

// One bulk block as an editor would frame it, checksum included.
static std::vector<uint8_t> bulk(uint8_t model, uint8_t ah, uint8_t am, uint8_t al, const uint8_t* data, uint8_t len)
{
    const uint16_t bc = (uint16_t)(4 + len);
    std::vector<uint8_t> m = {0xF0, 0x43, 0x00, 0x7F, 0x1C,
                              (uint8_t)((bc >> 7) & 0x7F), (uint8_t)(bc & 0x7F),
                              model, ah, am, al};
    uint32_t sum = model + ah + am + al;
    for (uint8_t k = 0; k < len; k++) { m.push_back(data[k] & 0x7F); sum += data[k] & 0x7F; }
    m.push_back((uint8_t)((0x80 - (sum & 0x7F)) & 0x7F));
    m.push_back(0xF7);
    return m;
}

// Verify one received bulk message the way Soundmondo's parser does: frame,
// byte count, model, address, checksum; returns the data bytes.
static bool parseBulk(const std::vector<uint8_t>& d, uint8_t ah, uint8_t am, uint8_t al, std::vector<uint8_t>& data)
{
    if (d.size() < 13 || d[0] != 0xF0 || d[1] != 0x43 || d[2] != 0x00 || d[3] != 0x7F || d[4] != 0x1C) return false;
    const uint16_t bc = (uint16_t)(((d[5] & 0x7F) << 7) | (d[6] & 0x7F));
    if (d.size() != (size_t)(bc + 9)) { printf("    byte count %u vs length %zu\n", bc, d.size()); return false; }
    if (d[7] != 0x06) { printf("    model byte %02X\n", d[7]); return false; }
    if (d[8] != ah || d[9] != am || d[10] != al) { printf("    address %02X %02X %02X\n", d[8], d[9], d[10]); return false; }
    uint32_t sum = 0;
    for (size_t k = 7; k < d.size() - 1; k++) sum += d[k];
    if (sum & 0x7F) { printf("    checksum\n"); return false; }
    data.assign(d.begin() + 11, d.end() - 2);
    return true;
}

// The TG block for a state, in data list order.
static std::vector<uint8_t> tgOf(const yc_engine_state_t& s)
{
    std::vector<uint8_t> b = { s.volume, 0, s.wave };
    for (int i = 0; i < 9; i++) b.push_back(s.footage[i]);
    b.push_back(s.vibcho_select); b.push_back(s.vibcho_depth);
    b.push_back(s.perc_on); b.push_back(s.perc_type); b.push_back(s.perc_length);
    b.push_back(s.rotary_speed); b.push_back(s.distortion); b.push_back(s.reverb);
    b.push_back(0); b.push_back(0);
    return b;
}

static void applyPattern(yc_engine_state_t& s, int seed)
{
    s.volume = (uint8_t)(100 + seed);
    s.wave = (uint8_t)((3 + seed) % 5);
    for (int i = 0; i < 9; i++) s.footage[i] = (uint8_t)((6 - i + seed + 9) % 7);
    s.vibcho_select = (uint8_t)((1 + seed) % 2);
    s.vibcho_depth = (uint8_t)((4 + seed) % 5);
    s.perc_on = (uint8_t)((1 + seed) % 2);
    s.perc_type = (uint8_t)(seed % 2);
    s.perc_length = (uint8_t)((3 + seed) % 5);
    s.rotary_speed = (uint8_t)((2 + seed) % 4);
    s.distortion = (uint8_t)(77 + seed);
    s.reverb = (uint8_t)(99 + seed);
}

int main()
{
    YC_Synth_Bridge bridge;
    yc_engine_state_t& st = bridge.state();
    yc_engine_init(st);
    RefaceMidi rm;
    rm.init(&bridge);

    auto feed = [&](const std::vector<uint8_t>& m) { rm.onSysEx(m.data(), (uint16_t) m.size()); };
    auto drain = [&]() { yc_ipc_drain(bridge); };

    // ---------- identity ----------
    printf("identity\n");
    {
        g_out.clear();
        const uint8_t req[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
        rm.onSysEx(req, sizeof req);
        const std::vector<uint8_t> want = {0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x43, 0x00, 0x41, 0x54, 0x06, 0x03, 0x00, 0x00, 0x7F, 0xF7};
        const bool ok = g_out == want;
        check(ok, "identity reply: 15 bytes, family 41 54 06 (data list 3-4-1-2; Soundmondo checks bytes 8/9 = 54 06)");
        if (!ok) { printf("    got"); hex(g_out); }
    }

    // ---------- TX: dump request on the bulk header ----------
    printf("TX: dump request 0E 0F 00 (what Soundmondo sends on connect)\n");
    {
        applyPattern(st, 0);
        g_out.clear();
        const uint8_t req[] = {0xF0, 0x43, 0x20, 0x7F, 0x1C, 0x06, 0x0E, 0x0F, 0x00, 0xF7};
        rm.onSysEx(req, sizeof req);
        auto ms = messages(g_out);
        check(ms.size() == 3, "three messages: header, TG block, footer");
        if (ms.size() == 3) {
            std::vector<uint8_t> d;
            check(parseBulk(ms[0], 0x0E, 0x0F, 0x00, d) && d.empty(), "bulk header 0E 0F 00, byte count 4");
            check(parseBulk(ms[1], 0x30, 0x00, 0x00, d) && d.size() == 22, "TG block 30 00 00, 22 data bytes, byte count 26");
            const bool same = d == tgOf(st);
            check(same, "TG block equals the engine state, data list order");
            if (!same) { printf("    want"); hex(tgOf(st)); printf("    got "); hex(d); }
            check(parseBulk(ms[2], 0x0F, 0x0F, 0x00, d) && d.empty(), "bulk footer 0F 0F 00, byte count 4");
        }
    }

    printf("TX: dump request 30 00 00 and 00 00 00\n");
    {
        g_out.clear();
        const uint8_t req[] = {0xF0, 0x43, 0x20, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x00, 0xF7};
        rm.onSysEx(req, sizeof req);
        auto ms = messages(g_out);
        std::vector<uint8_t> d;
        check(ms.size() == 1 && parseBulk(ms[0], 0x30, 0x00, 0x00, d) && d == tgOf(st), "TG block alone");

        g_out.clear();
        const uint8_t sys[] = {0xF0, 0x43, 0x20, 0x7F, 0x1C, 0x06, 0x00, 0x00, 0x00, 0xF7};
        rm.onSysEx(sys, sizeof sys);
        ms = messages(g_out);
        check(ms.size() == 1 && parseBulk(ms[0], 0x00, 0x00, 0x00, d) && d.size() == 32, "SYSTEM block, 32 bytes, byte count 36 (shared layer)");
    }

    // ---------- RX: bulk dump from an editor ----------
    printf("RX: bulk dump sent by an editor\n");
    {
        yc_engine_state_t want{};
        yc_engine_init(want);
        applyPattern(want, 1);
        const auto block = tgOf(want);
        feed(bulk(0x06, 0x0E, 0x0F, 0x00, nullptr, 0));
        feed(bulk(0x06, 0x30, 0x00, 0x00, block.data(), (uint8_t) block.size()));
        feed(bulk(0x06, 0x0F, 0x0F, 0x00, nullptr, 0));
        drain();
        const bool same = tgOf(st) == block;
        check(same, "every TG byte reached the engine");
        if (!same) { printf("    want"); hex(block); printf("    got "); hex(tgOf(st)); }

        // Out-of-range bytes are clamped to the data list's ranges, not indexed with.
        std::vector<uint8_t> wild(22, 0x7F);
        feed(bulk(0x06, 0x30, 0x00, 0x00, wild.data(), 22));
        drain();
        bool clamped = st.wave == 4 && st.vibcho_select == 1 && st.vibcho_depth == 4 && st.perc_on == 1
                    && st.perc_type == 1 && st.perc_length == 4 && st.rotary_speed == 3
                    && st.distortion == 127 && st.reverb == 127 && st.volume == 127;
        for (int i = 0; i < 9; i++) clamped = clamped && st.footage[i] == 6;
        check(clamped, "a block of 7F lands as the maximum of every range");

        // Wrong model byte: ignored whole.
        applyPattern(st, 0);
        feed(bulk(0x05, 0x30, 0x00, 0x00, block.data(), (uint8_t) block.size()));
        drain();
        check(tgOf(st) != block && st.wave == 3, "model byte 05 (reface DX): block ignored");

        // Bad checksum: ignored whole (shared layer).
        auto bad = bulk(0x06, 0x30, 0x00, 0x00, block.data(), (uint8_t) block.size());
        bad[bad.size() - 2] ^= 0x01;
        feed(bad);
        drain();
        check(st.wave == 3, "bad checksum: block ignored");
    }

    // ---------- parameter change / request ----------
    printf("parameter change and request on the TG block\n");
    {
        const uint8_t set5[] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x03, 0x05, 0xF7};
        rm.onSysEx(set5, sizeof set5); drain();
        check(st.footage[0] == 5, "30 00 03 = 5 sets footage 16' to 5");

        const uint8_t set7f[] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x03, 0x7F, 0xF7};
        rm.onSysEx(set7f, sizeof set7f); drain();
        check(st.footage[0] == 6, "30 00 03 = 7F is clamped to 6 (was an out-of-bounds LUT read, section 39)");

        const uint8_t rot[] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x11, 0x02, 0xF7};
        rm.onSysEx(rot, sizeof rot); drain();
        check(st.rotary_speed == 2, "30 00 11 = 2 sets the rotary to SLOW");

        const uint8_t vol[] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x00, 0x40, 0xF7};
        rm.onSysEx(vol, sizeof vol); drain();
        check(st.volume == 64 && std::fabs(st.vol_gain - 64.0f / 127.0f) < 1e-6f, "30 00 00 = 40 sets the volume and the gain");

        const uint8_t dx[] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x05, 0x30, 0x00, 0x03, 0x01, 0xF7};
        rm.onSysEx(dx, sizeof dx); drain();
        check(st.footage[0] == 6, "model byte 05: parameter change ignored");

        st.distortion = 77;
        g_out.clear();
        const uint8_t req[] = {0xF0, 0x43, 0x30, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x12, 0xF7};
        rm.onSysEx(req, sizeof req);
        const std::vector<uint8_t> want = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x12, 77, 0xF7};
        check(g_out == want, "request 30 00 12 is answered with one Parameter Change carrying the value");

        g_out.clear();
        const uint8_t reqres[] = {0xF0, 0x43, 0x30, 0x7F, 0x1C, 0x06, 0x30, 0x00, 0x16, 0xF7};
        rm.onSysEx(reqres, sizeof reqres);
        check(g_out.empty(), "request past the block (30 00 16) is not answered");
    }

    // ---------- controllers recognised without MIDI Control ----------
    printf("controllers\n");
    {
        yc_engine_init(st);
        rm.setMidiControlEnabled(false);   // none of these depend on it

        rm.onPitchBend(16383, 0); drain();
        check(std::fabs(st.bend_ratio - std::pow(2.0f, 2.0f / 12.0f)) < 1e-4f, "pitch bend max reaches the engine as +2 semitones");
        rm.onPitchBend(8192, 0); drain();
        check(st.bend_ratio == 1.0f, "pitch bend centre: factor 1.0");

        rm.onControlChange(1, 100, 0); drain();
        check(st.rotary_speed == 3, "CC1 (mod wheel) >= 64: rotary FAST");
        rm.onControlChange(1, 10, 0); drain();
        check(st.rotary_speed == 2, "CC1 < 64: rotary SLOW");

        rm.onControlChange(7, 64, 0); drain();
        check(st.midi_volume == 64 && std::fabs(st.vol_gain - 64.0f / 127.0f) < 1e-6f, "CC7 64: channel volume into the gain");
        rm.onControlChange(11, 0, 0); drain();
        check(st.expression == 0 && st.vol_gain == 0.0f, "CC11 0: expression mutes");
        rm.onControlChange(64, 127, 0); drain();
        check(st.sustain_held, "CC64 127: sustain on, MIDI Control off or not");

        rm.onPitchBend(0, 0); drain();
        rm.onControlChange(121, 0, 0); drain();
        check(st.bend_ratio == 1.0f && st.expression == 127 && !st.sustain_held && st.midi_volume == 64,
              "CC121 reset all controllers: bend centred, expression 127, sustain off, volume kept");
        check(st.rotary_speed == 2, "CC121 leaves the rotary where it is");

        rm.onNoteOn(60, 100, 0); rm.onNoteOn(64, 100, 0); drain();
        check(st.active_count == 2, "two notes on");
        rm.onControlChange(120, 0, 0); drain();
        check(st.active_count == 0, "CC120 all sound off silences them");
        rm.onNoteOn(60, 100, 0); drain();
        rm.onControlChange(126, 0, 0); drain();
        check(st.active_count == 0, "CC126 mono acts as all sound off (data list 3-2-6)");

        // Panel CCs stay gated.
        rm.onControlChange(80, 127, 0); drain();
        check(st.wave == 0, "CC80 with MIDI Control off: ignored");
        rm.setMidiControlEnabled(true);
        rm.onControlChange(80, 127, 0); drain();
        check(st.wave == 4, "CC80 127 with MIDI Control on: wave Y");
        rm.onControlChange(105, 64, 0); drain();
        check(st.footage[3] == 3, "CC105 64: footage 4' slider 3 (bin 55-73)");
    }

    // ---------- panel -> MIDI OUT ----------
    printf("panel mirror\n");
    {
        rm.setMidiControlEnabled(true);
        g_out.clear();
        rm.txPanelMirror(YC_PARAM_FOOTAGE_16, 6);
        rm.txPanelMirror(YC_PARAM_VIBCHO_DEPTH, 2);
        rm.txRotaryMirror(3);
        rm.txPanelMirror(YC_PARAM_VOLUME, 100);
        rm.txPanelMirror(YC_PARAM_OCTAVE, 1);
        const std::vector<uint8_t> want = {0xB0, 102, 127, 0xB0, 77, 64, 0xB0, 19, 127};
        check(g_out == want, "footage 16' 6 -> CC102 127, depth 2 -> CC77 64, rotary FAST -> CC19 127; volume and octave have no CC");
        if (g_out != want) { printf("    got"); hex(g_out); }

        rm.setMidiControlEnabled(false);
        g_out.clear();
        rm.txPanelMirror(YC_PARAM_FOOTAGE_16, 6);
        rm.txRotaryMirror(2);
        check(g_out.empty(), "MIDI Control off: nothing mirrored");
    }

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
