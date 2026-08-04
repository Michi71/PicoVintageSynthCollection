// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  juno_test.cpp  --  macOS host test for the Juno engine (PicoFaceJ6)

  Plays the (unmodified) Pico engine from include/juno + src/juno on the Mac
  through CoreAudio and drives it over MIDI (PortMidi virtual input "juno").

  Controls:
    + / -   change patch
    p       print the whole front panel, section by section
    m       print the MIDI controller map
    d       hold / release a chord      a  arpeggio across the keyboard
    s       sustain pedal (CC 64)       v  how many voice cards are in use
    DCO:  1 range   2 sawtooth   3 pulse   4 sub      w/W pulse width
    VCF:  f/F cutoff   r/R resonance   e/E contour   k/K key follow   o polarity
    ENV:  A/D/S/E raise attack/decay/sustain/release, z lowers the last one
    h       high-pass filter            c  chorus (off / I / II / I+II)
    g       VCA between contour and gate
    ARP:  j on/off   n mode   b range   ,/. rate down/up   l hold
    t       self test                   x  quit

  Build:  ./test/build_juno.sh
          ./test/juno_test --selftest    (no audio device, no MIDI, no terminal)
*/

#include <AudioToolbox/AudioToolbox.h>
#include <portmidi.h>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <atomic>
#include <algorithm>
#include <iostream>

#include "juno/juno.h"

static const uint32_t HOST_SR = 44100;
static Juno* synth = nullptr;

/* --- Audio (CoreAudio) --------------------------------------------------- */
static AudioUnit audioUnit;
static int16_t ringL[I2S_BUFFER_WORDS], ringR[I2S_BUFFER_WORDS];
static UInt32 ringFill = 0, ringPos = 0;

static OSStatus renderCallback(void*, AudioUnitRenderActionFlags*,
                               const AudioTimeStamp*, UInt32,
                               UInt32 inNumberFrames, AudioBufferList* ioData) {
    float* out = reinterpret_cast<float*>(ioData->mBuffers[0].mData);
    for (UInt32 n = 0; n < inNumberFrames; ++n) {
        if (ringPos >= ringFill) {
            synth->process(ringR, ringL);
            ringPos = 0; ringFill = I2S_BUFFER_WORDS;
        }
        out[n*2]   = ringL[ringPos] / 32768.0f;
        out[n*2+1] = ringR[ringPos] / 32768.0f;
        ringPos++;
    }
    return noErr;
}

static bool initAudio() {
    AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                      kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) return false;
    AudioComponentInstanceNew(comp, &audioUnit);
    AudioStreamBasicDescription format = {};
    format.mSampleRate = HOST_SR; format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mChannelsPerFrame = 2; format.mBitsPerChannel = 32;
    format.mBytesPerFrame = 8; format.mFramesPerPacket = 1; format.mBytesPerPacket = 8;
    AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Input, 0, &format, sizeof(format));
    AURenderCallbackStruct cb = {renderCallback, NULL};
    AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                         kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    AudioUnitInitialize(audioUnit);
    return true;
}

/* --- MIDI (PortMidi) ----------------------------------------------------- */
static PmStream* midiInStream = nullptr;
static const char* VIRT_NAME = "juno";

static void sendMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    switch (status & 0xF0) {
        case 0x80: synth->noteOff(d1); break;
        case 0x90: (d2 > 0) ? synth->noteOn(d1, d2) : synth->noteOff(d1); break;
        case 0xB0: synth->processMidiController(d1, d2); break;
        case 0xC0: synth->setProgram(d1 % synth->getProgramCount()); break;
        case 0xE0: synth->setPitchBend(((int)d2 << 7) | d1); break;
        default: break;
    }
}

static bool initMIDI() {
    if (Pm_Initialize() != pmNoError) return false;
    int in_id = Pm_CreateVirtualInput(VIRT_NAME, NULL, NULL);
    if (in_id < 0) { std::cerr << "Virtual MIDI input failed\n"; return false; }
    if (Pm_OpenInput(&midiInStream, in_id, NULL, 0, NULL, NULL) != pmNoError) {
        std::cerr << "Open MIDI input failed\n"; return false;
    }
    return true;
}

static void updateMIDI() {
    if (!midiInStream) return;
    PmEvent e;
    while (Pm_Read(midiInStream, &e, 1) > 0)
        sendMidi(Pm_MessageStatus(e.message), Pm_MessageData1(e.message),
                 Pm_MessageData2(e.message));
}

/* --- Keyboard ------------------------------------------------------------ */
static std::atomic<char> lastKey{0};
static char getch() {
    struct termios oldt, newt; tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt; newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return c;
}
static void inputThreadFunc() { while (true) { char c = getch(); lastKey = c; if (c == 'x') break; } }

/* --- Reporting ----------------------------------------------------------- */
static void printStatus() {
    char nm[24] = {0}; synth->getProgramName(nm);
    char rng[16], cut[16], res[16], ch[16];
    synth->getParameterDisplay(JUNO_DCO_RANGE, rng);
    synth->getParameterDisplay(JUNO_VCF_FREQ,  cut);
    synth->getParameterDisplay(JUNO_VCF_RES,   res);
    synth->getParameterDisplay(JUNO_CHORUS,    ch);
    std::printf("Patch %d/%d: %-16s  %s  cutoff %s  res %s  chorus %s\n",
                synth->getProgram() + 1, (int) synth->getProgramCount(), nm,
                rng, cut, res, ch);
}

static void printParams() {
    struct { const char* title; int first, last; } sections[] = {
        { "LFO",    JUNO_LFO_RATE,   JUNO_LFO_DELAY   },
        { "DCO",    JUNO_DCO_RANGE,  JUNO_DCO_NOISE   },
        { "HPF",    JUNO_HPF,        JUNO_HPF         },
        { "VCF",    JUNO_VCF_FREQ,   JUNO_VCF_KYBD    },
        { "VCA",    JUNO_VCA_LEVEL,  JUNO_VCA_MODE    },
        { "ENV",    JUNO_ENV_ATTACK, JUNO_ENV_RELEASE },
        { "CHORUS", JUNO_CHORUS,     JUNO_CHORUS      },
        { "SYSTEM", JUNO_TUNE,       JUNO_TRANSPOSE   },
        /* Instrument settings: no patch change touches these. */
        { "ARP",    JUNO_ARP_ON,     JUNO_ARP_RATE    },
        { "OUTPUT", JUNO_HOLD,       JUNO_MASTER      },
    };
    char n[24], d[24];
    for (auto& s : sections) {
        std::printf("--- %s %.*s\n", s.title, (int) (44 - strlen(s.title)),
                    "-------------------------------------------------");
        for (int i = s.first; i <= s.last; ++i) {
            synth->getParameterName(i, n);
            synth->getParameterDisplay(i, d);
            std::printf("  %2d  %-10s %10s\n", i, n, d);
        }
    }
}

static void printCcMap() {
    std::printf("--- MIDI controllers ------------------------------\n");
    for (int i = 0; i < JUNO_TOTAL_COUNT; ++i) {
        const JunoParamDesc& p = kJunoParams[i];
        if (p.cc == JUNO_CC_NONE) continue;
        std::printf("  CC %3d  %-10s (param %d)\n", p.cc, p.name, i);
    }
    std::printf("  CC  64  sustain pedal\n"
                "  CC 120  all sound off      CC 123  all notes off\n"
                "  CC 121  reset controllers\n");
}

/* --- Editing helpers ----------------------------------------------------- */
static int lastEnvParam = JUNO_ENV_ATTACK;

static void cycleEnum(int id) {
    const JunoParamDesc& d = kJunoParams[id];
    const int steps = (d.steps < 2) ? 2 : d.steps;
    int s = junoParamStep(synth->getParameter(id), steps) + 1;
    if (s >= steps) s = 0;
    synth->setParameter(id, junoParamFromStep(s, steps));
    char n[24], v[24];
    synth->getParameterName(id, n); synth->getParameterDisplay(id, v);
    std::printf("%-10s %s\n", n, v);
}

static void nudge(int id, float delta) {
    float v = synth->getParameter(id) + delta;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    synth->setParameter(id, v);
    char n[24], s[24];
    synth->getParameterName(id, n); synth->getParameterDisplay(id, s);
    std::printf("%-10s %s\n", n, s);
}

static void envUp(int id) { lastEnvParam = id; nudge(id, +0.05f); }

static bool chordDown = false;
static const int kChord[4] = { 48, 52, 55, 60 };
static void toggleChord() {
    for (int k : kChord) chordDown ? synth->noteOff(k) : synth->noteOn(k, 100);
    chordDown = !chordDown;
    std::printf("%s  (%d voices active)\n",
                chordDown ? "Chord on" : "Chord off", synth->activeVoices());
}

static void arpeggio() {
    std::printf("Arpeggio C2..C7\n");
    for (int n = JUNO_KEY_FIRST; n <= JUNO_KEY_LAST; n += 2) {
        synth->noteOn(n, 100);
        usleep(80000);
        synth->noteOff(n);
    }
}

/* --- Self test ----------------------------------------------------------- */
static void selfTest() {
    Juno probe;
    probe.setSampleRate((float) HOST_SR);

    std::printf("--- self test, %d patches ---------------------------\n",
                (int) probe.getProgramCount());
    std::printf("%-16s %8s %8s %6s %8s\n", "patch", "peak", "rms", "nan", "L/R cor");

    int bad = 0;
    float l[64], r[64];

    for (int p = 0; p < probe.getProgramCount(); ++p) {
        probe.resetVoices();
        probe.setProgram(p);
        char nm[24]; probe.getProgramName(nm);

        for (int d = 0; d < 4410; d += 64) probe.processFloat(l, r, 64);
        for (int k : kChord) probe.noteOn(k, 100);

        float peak = 0.0f; double rms = 0.0, sl = 0.0, sr = 0.0, slr = 0.0;
        int nan = 0, frames = 0;
        for (int d = 0; d < 44100; d += 64) {
            probe.processFloat(l, r, 64);
            for (int i = 0; i < 64; ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { nan++; continue; }
                peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
                rms += (double) l[i] * l[i];
                sl += l[i]*l[i]; sr += r[i]*r[i]; slr += l[i]*r[i];
                frames++;
            }
        }
        for (int k : kChord) probe.noteOff(k);
        rms = frames ? std::sqrt(rms / frames) : 0.0;
        const double cor = slr / std::sqrt(sl * sr + 1e-20);

        const bool fail = nan || peak > 1.01f || rms < 0.0002;
        std::printf("%-16s %8.4f %8.4f %6d %8.3f%s\n",
                    nm, peak, rms, nan, cor, fail ? "   <-- FAIL" : "");
        if (fail) bad++;
    }

    /* Nothing the panel can be set to may make it blow up. */
    for (int pass = 0; pass < 2; ++pass) {
        probe.resetVoices();
        const float v = pass ? 1.0f : 0.0f;
        for (int i = 0; i < probe.getParameterCount(); ++i) probe.setParameter(i, v);
        for (int k = 0; k < JUNO_VOICES; ++k) probe.noteOn(40 + k * 5, 127);

        float peak = 0.0f; int nan = 0;
        for (int d = 0; d < 44100 * 2; d += 64) {
            probe.processFloat(l, r, 64);
            for (int i = 0; i < 64; ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { nan++; continue; }
                peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
            }
        }
        const bool fail = nan || peak > 1.01f;
        std::printf("all parameters at %.0f: peak %.4f nan %d%s\n",
                    v, peak, nan, fail ? "   <-- FAIL" : "");
        if (fail) bad++;
    }

    /*
     * The six-voice limit. Eight notes are held; six cards should sound and no
     * seventh should quietly appear. This is the one thing that is specific to
     * a Juno rather than to synthesisers in general, so it gets its own test.
     */
    std::printf("--- the six-voice limit ---------------------------\n");
    probe.resetVoices();
    probe.setProgram(0);
    probe.setParameter(JUNO_ENV_SUSTAIN, 1.0f);
    probe.setParameter(JUNO_ENV_ATTACK,  0.0f);
    probe.setParameter(JUNO_VCA_MODE, junoParamFromStep(0, 2));
    for (int n = 0; n < 8; ++n) {
        probe.noteOn(48 + n * 2, 100);
        for (int d = 0; d < 2205; d += 64) probe.processFloat(l, r, 64);
        const int act = probe.activeVoices();
        std::printf("  %d notes held -> %d voices active%s\n",
                    n + 1, act, act > JUNO_VOICES ? "   <-- FAIL" : "");
        if (act > JUNO_VOICES) bad++;
    }
    if (probe.activeVoices() != JUNO_VOICES) {
        std::printf("  !! expected all %d cards in use, got %d\n",
                    JUNO_VOICES, probe.activeVoices());
        bad++;
    }

    std::printf("%s\n", bad ? "SELF TEST FAILED" : "self test passed");
}

/* ------------------------------------------------------------------------ */
int main(int argc, char** argv) {
    synth = new Juno();
    synth->setSampleRate((float) HOST_SR);
    synth->setProgram(0);

    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) {
        selfTest();
        printCcMap();
        printParams();
        delete synth;
        return 0;
    }

    if (!initAudio()) { fprintf(stderr, "Audio init failed.\n"); return 1; }
    if (!initMIDI())  { fprintf(stderr, "MIDI init failed (continuing without MIDI).\n"); }
    AudioOutputUnitStart(audioUnit);

    std::cout << "Roland Juno-60 -- host test  (SR=" << HOST_SR
              << ", block=" << I2S_BUFFER_WORDS
              << ", " << JUNO_OVERSAMPLE << "x oversampled, "
              << JUNO_VOICES << " voices)\n";
    std::cout << "MIDI: send to the virtual port \"" << VIRT_NAME << "\".\n";
    printStatus();

    std::thread inputThread(inputThreadFunc);
    printf("+/- patch | 1 range | 2 saw | 3 pulse | 4 sub | w/W pulse width\n"
           "f/F cutoff | r/R res | e/E contour | k/K key follow | o polarity\n"
           "A/D/S/E env up, z down | h hpf | c chorus | g vca mode\n"
           "j arp | n mode | b range | ,/. arp rate | l hold\n"
           "d chord | a arpeggio | s sustain | v voices | p panel | m cc map\n"
           "t self test | x quit\n");

    bool sustainToggle = false, quit = false;
    while (!quit) {
        updateMIDI();
        char c = lastKey.exchange(0);
        switch (c) {
            case '+': { int p = (synth->getProgram() + 1) % synth->getProgramCount();
                        synth->setProgram(p); printStatus(); break; }
            case '-': { int p = synth->getProgram() - 1;
                        if (p < 0) p = synth->getProgramCount() - 1;
                        synth->setProgram(p); printStatus(); break; }

            case '1': cycleEnum(JUNO_DCO_RANGE);  break;
            case '2': cycleEnum(JUNO_DCO_SAW);    break;
            case '3': cycleEnum(JUNO_DCO_PULSE);  break;
            case '4': cycleEnum(JUNO_DCO_SUB);    break;
            case 'w': nudge(JUNO_DCO_PWM, -0.05f); break;
            case 'W': nudge(JUNO_DCO_PWM, +0.05f); break;

            case 'f': nudge(JUNO_VCF_FREQ, -0.03f); break;
            case 'F': nudge(JUNO_VCF_FREQ, +0.03f); break;
            case 'r': nudge(JUNO_VCF_RES,  -0.03f); break;
            case 'R': nudge(JUNO_VCF_RES,  +0.03f); break;
            case 'e': nudge(JUNO_VCF_ENV,  -0.03f); break;
            case 'E': envUp(JUNO_ENV_RELEASE);      break;
            case 'k': nudge(JUNO_VCF_KYBD, -0.05f); break;
            case 'K': nudge(JUNO_VCF_KYBD, +0.05f); break;
            case 'o': cycleEnum(JUNO_VCF_POLARITY); break;

            case 'A': envUp(JUNO_ENV_ATTACK);  break;
            case 'D': envUp(JUNO_ENV_DECAY);   break;
            case 'S': envUp(JUNO_ENV_SUSTAIN); break;
            case 'z': nudge(lastEnvParam, -0.05f); break;

            case 'h': cycleEnum(JUNO_HPF);      break;
            case 'c': cycleEnum(JUNO_CHORUS);   break;
            case 'g': cycleEnum(JUNO_VCA_MODE); break;

            case 'j': cycleEnum(JUNO_ARP_ON);    break;
            case 'n': cycleEnum(JUNO_ARP_MODE);  break;
            case 'b': cycleEnum(JUNO_ARP_RANGE); break;
            case 'l': cycleEnum(JUNO_HOLD);      break;
            case ',': nudge(JUNO_ARP_RATE, -0.03f); break;
            case '.': nudge(JUNO_ARP_RATE, +0.03f); break;

            case 'd': toggleChord(); break;
            case 'a': arpeggio();    break;
            case 'v': std::printf("%d of %d voice cards active\n",
                                  synth->activeVoices(), JUNO_VOICES); break;
            case 'p': printParams(); break;
            case 'm': printCcMap();  break;
            case 't': selfTest();    break;

            case 's': sustainToggle = !sustainToggle;
                      synth->processMidiController(0x40, sustainToggle ? 127 : 0);
                      std::cout << "Sustain " << (sustainToggle ? "ON" : "OFF") << std::endl;
                      break;

            case 'x': quit = true; break;
            default: break;
        }
        usleep(10000);
    }

    inputThread.join();
    AudioOutputUnitStop(audioUnit); AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
    if (midiInStream) Pm_Close(midiInStream);
    Pm_Terminate(); delete synth; return 0;
}
