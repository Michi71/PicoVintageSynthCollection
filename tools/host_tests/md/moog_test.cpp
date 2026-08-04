// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/*
  moog_test.cpp  --  macOS host test for the Moog engine (PicoFaceMD)

  Plays the (unmodified) Pico engine from include/moog + src/moog on the Mac
  through CoreAudio and drives it over MIDI (PortMidi virtual input "moog").

  Controls:
    + / -   change preset
    p       print the whole front panel, section by section
    m       print the MIDI controller map
    d       hold / release a note
    a       arpeggio across the keyboard
    s       sustain pedal (CC 64) on/off
    Oscillator bank:
      1/2/3 cycle the waveform of oscillator 1 / 2 / 3
      q/w/e cycle the range of oscillator 1 / 2 / 3
    Modifiers:
      f/F   filter cutoff down / up
      r/R   emphasis down / up
      c/C   amount of contour down / up
    g       glide switch          o  oscillator modulation switch
    k       osc 3 keyboard switch j  filter modulation switch
    </>     modulation wheel down / up
    t       self test: every preset checked for level and for NaN
    x       quit

  Build:  ./test/build_moog.sh
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
#include <iostream>

#include "moog/moog.h"

static const uint32_t HOST_SR = 44100;
static Moog* synth = nullptr;

// ---------------------------------------------------------------------------
// Audio (CoreAudio) -- ring buffer: engine blocks consumed without loss
// ---------------------------------------------------------------------------
static AudioUnit audioUnit;
static int16_t ringL[I2S_BUFFER_WORDS], ringR[I2S_BUFFER_WORDS];
static UInt32 ringFill = 0, ringPos = 0;

static OSStatus renderCallback(void*, AudioUnitRenderActionFlags*,
                               const AudioTimeStamp*, UInt32,
                               UInt32 inNumberFrames, AudioBufferList* ioData) {
    float* out = reinterpret_cast<float*>(ioData->mBuffers[0].mData);
    for (UInt32 n = 0; n < inNumberFrames; ++n) {
        if (ringPos >= ringFill) { synth->process(ringR, ringL); ringPos = 0; ringFill = I2S_BUFFER_WORDS; }
        out[n*2]   = ringL[ringPos] / 32768.0f;
        out[n*2+1] = ringR[ringPos] / 32768.0f;
        ringPos++;
    }
    return noErr;
}

bool initAudio() {
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

// ---------------------------------------------------------------------------
// MIDI (PortMidi)
// ---------------------------------------------------------------------------
static PmStream* midiInStream = nullptr;
static const char* VIRT_NAME = "moog";

void sendMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    switch (status & 0xF0) {
        case 0x80: synth->noteOff(d1); break;
        case 0x90: (d2 > 0) ? synth->noteOn(d1, d2) : synth->noteOff(d1); break;
        case 0xB0: synth->processMidiController(d1, d2); break;
        case 0xC0: synth->setProgram(d1 % synth->getProgramCount()); break;
        case 0xE0: synth->setPitchBend(((int)d2 << 7) | d1); break;
        default: break;
    }
}

bool initMIDI() {
    if (Pm_Initialize() != pmNoError) return false;
    int in_id = Pm_CreateVirtualInput(VIRT_NAME, NULL, NULL);
    if (in_id < 0) { std::cerr << "Virtual MIDI input failed\n"; return false; }
    if (Pm_OpenInput(&midiInStream, in_id, NULL, 0, NULL, NULL) != pmNoError) {
        std::cerr << "Open MIDI input failed\n"; return false;
    }
    return true;
}

void updateMIDI() {
    if (!midiInStream) return;
    PmEvent event;
    while (Pm_Read(midiInStream, &event, 1) > 0)
        sendMidi(Pm_MessageStatus(event.message), Pm_MessageData1(event.message),
                 Pm_MessageData2(event.message));
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
std::atomic<char> lastKey{0};
char getch() {
    struct termios oldt, newt; tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt; newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return c;
}
void inputThreadFunc() { while (true) { char c = getch(); lastKey = c; if (c == 'x') break; } }

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------
static void printStatus() {
    char pname[24] = {0}; synth->getProgramName(pname);
    char o1[16], o2[16], o3[16], cut[16], emp[16];
    synth->getParameterDisplay(MOOG_OSC1_WAVE, o1);
    synth->getParameterDisplay(MOOG_OSC2_WAVE, o2);
    synth->getParameterDisplay(MOOG_OSC3_WAVE, o3);
    synth->getParameterDisplay(MOOG_CUTOFF,    cut);
    synth->getParameterDisplay(MOOG_EMPHASIS,  emp);
    std::printf("Preset %d/%d: %-14s  osc %s/%s/%s  cutoff %s  emphasis %s\n",
                synth->getProgram() + 1, synth->getProgramCount(), pname,
                o1, o2, o3, cut, emp);
}

// The front panel, grouped the way the instrument is.
static void printParams() {
    struct { const char* title; int first, last; } sections[] = {
        { "CONTROLLERS",     MOOG_TUNE,       MOOG_BEND_RANGE   },
        { "OSCILLATOR BANK", MOOG_OSC1_RANGE, MOOG_OSC3_WAVE    },
        { "MIXER",           MOOG_OSC1_VOL,   MOOG_FEEDBACK_ON  },
        { "MODIFIERS",       MOOG_CUTOFF,     MOOG_DECAY_SW     },
        { "OUTPUT",          MOOG_VOLUME,     MOOG_A440         },
        { "VOICING",         MOOG_DRIVE,      MOOG_TRANSPOSE    },
    };
    char n[24], d[24];
    for (auto& s : sections) {
        std::printf("--- %s %.*s\n", s.title,
                    (int) (46 - strlen(s.title)),
                    "--------------------------------------------------");
        for (int i = s.first; i <= s.last; ++i) {
            synth->getParameterName(i, n);
            synth->getParameterDisplay(i, d);
            std::printf("  %2d  %-10s %10s\n", i, n, d);
        }
    }
}

static void printCcMap() {
    std::printf("--- MIDI controllers ------------------------------\n");
    for (int i = 0; i < MOOG_PARAM_COUNT; ++i) {
        const MoogParamDesc& p = kMoogParams[i];
        if (p.cc == MOOG_CC_NONE) continue;
        std::printf("  CC %3d  %-10s (param %d)\n", p.cc, p.name, i);
    }
    std::printf("  CC  64  sustain pedal\n"
                "  CC 120  all sound off      CC 123  all notes off\n"
                "  CC 121  reset controllers\n");
}

// ---------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------
static void cycleEnum(int id) {
    const MoogParamDesc& d = kMoogParams[id];
    const int steps = (d.steps < 2) ? 2 : d.steps;
    int s = moogParamStep(synth->getParameter(id), steps) + 1;
    if (s >= steps) s = 0;
    synth->setParameter(id, moogParamFromStep(s, steps));

    char n[24], v[24];
    synth->getParameterName(id, n);
    synth->getParameterDisplay(id, v);
    std::printf("%-10s %s\n", n, v);
}

static void nudge(int id, float delta) {
    float v = synth->getParameter(id) + delta;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    synth->setParameter(id, v);

    char n[24], s[24];
    synth->getParameterName(id, n);
    synth->getParameterDisplay(id, s);
    std::printf("%-10s %s\n", n, s);
}

// Glide is two controls that only make sense together: a time and a switch.
// Reporting both on every change makes it obvious why the switch does nothing
// when the time is at zero -- which is correct behaviour, and confusing until
// you can see it.
static void reportGlide() {
    char sw[16], t[16];
    synth->getParameterDisplay(MOOG_GLIDE_ON, sw);
    synth->getParameterDisplay(MOOG_GLIDE,    t);
    const float v = synth->getParameter(MOOG_GLIDE);
    std::printf("Glide switch %-3s  time %s  (%.0f ms)%s\n",
                sw, t, v * v * MOOG_GLIDE_MAX_S * 1000.0f,
                (v <= 0.0f) ? "   <- time is zero, the switch cannot do anything" : "");
}

// One note, not a chord: the instrument is monophonic and would sound only the
// lowest of them anyway.
//
// The pitch alternates between two notes an octave apart rather than repeating
// one. Glide only exists between two different pitches, so a key that played
// the same note every time could never demonstrate it -- and the control
// voltage persists across the silence, so the glide is heard on the second
// note even though the first was released.
static bool noteDown = false;
static const int kDemoNotes[2] = { 40, 52 };
static int demoIndex = 0;

static void toggleNote() {
    if (noteDown) {
        synth->noteOff(kDemoNotes[demoIndex]);
        demoIndex ^= 1;
        std::cout << "Note off\n";
    } else {
        synth->noteOn(kDemoNotes[demoIndex], 100);
        std::printf("Note on %d\n", kDemoNotes[demoIndex]);
    }
    noteDown = !noteDown;
}

static void arpeggio() {
    std::cout << "Arpeggio F1..C5\n";
    for (int n = MOOG_KEY_FIRST; n <= MOOG_KEY_LAST; n += 2) {
        synth->noteOn(n, 100);
        usleep(90000);
        synth->noteOff(n);
    }
}

// ---------------------------------------------------------------------------
// Self test -- the same checks the engine has to pass before it is flashed
// ---------------------------------------------------------------------------
static void selfTest() {
    // Runs on a second engine so the audio callback keeps playing the first.
    Moog probe;
    probe.setSampleRate((float) HOST_SR);

    std::printf("--- self test -------------------------------------\n");
    std::printf("%-14s %8s %8s %6s\n", "preset", "peak", "rms", "nan");

    int bad = 0;
    float l[64], r[64];

    for (int p = 0; p < probe.getProgramCount(); ++p) {
        probe.resetVoices();
        probe.setProgram(p);
        char name[24]; probe.getProgramName(name);

        float peak = 0.0f; double rms = 0.0; int nan = 0; int frames = 0;
        auto run = [&](int total, bool measure) {
            int done = 0;
            while (done < total) {
                probe.processFloat(l, r, 64);
                if (measure) for (int i = 0; i < 64; ++i) {
                    if (!std::isfinite(l[i])) { nan++; continue; }
                    float a = std::fabs(l[i]);
                    if (a > peak) peak = a;
                    rms += (double) l[i] * l[i];
                    frames++;
                }
                done += 64;
            }
        };

        run(4410, false);              // settle
        probe.noteOn(48, 100);
        run(22050, true);              // 500 ms held
        probe.noteOff(48);
        run(22050, true);              // 500 ms after release

        rms = frames ? std::sqrt(rms / frames) : 0.0;
        const bool fail = nan || peak > 1.2f || rms < 0.0005;
        std::printf("%-14s %8.4f %8.4f %6d%s\n",
                    name, peak, rms, nan, fail ? "   <-- FAIL" : "");
        if (fail) bad++;
    }

    // Nothing the panel can be set to may make the engine blow up.
    for (int pass = 0; pass < 2; ++pass) {
        probe.resetVoices();
        const float v = pass ? 1.0f : 0.0f;
        for (int i = 0; i < probe.getParameterCount(); ++i) probe.setParameter(i, v);
        probe.noteOn(pass ? 72 : 36, 127);

        float peak = 0.0f; int nan = 0;
        for (int done = 0; done < 44100; done += 64) {
            probe.processFloat(l, r, 64);
            for (int i = 0; i < 64; ++i) {
                if (!std::isfinite(l[i])) { nan++; continue; }
                float a = std::fabs(l[i]);
                if (a > peak) peak = a;
            }
        }
        const bool fail = nan || peak > 1.5f;
        std::printf("all parameters at %.0f: peak %.4f nan %d%s\n",
                    v, peak, nan, fail ? "   <-- FAIL" : "");
        if (fail) bad++;
    }

    // Every pair of effect slots, with each effect wound up as far as the
    // panel allows. Delay feedback and reverb size are recursive, and a
    // recursive path that is merely "usually stable" is a bug waiting for the
    // one setting nobody tried. The note is released early on purpose: what
    // is being watched is the tail, long after the voice has stopped feeding
    // it anything.
    std::printf("\n--- effects, both slots, everything at maximum ---\n");
    const char* kindName[MOOG_FX_KIND_COUNT] = { "off", "chorus", "delay", "reverb" };

    for (int a = 0; a < MOOG_FX_KIND_COUNT; ++a) {
        for (int b = 0; b < MOOG_FX_KIND_COUNT; ++b) {
            probe.resetVoices();
            probe.setProgram(0);
            probe.setParameter(MOOG_FX_SLOT_A, moogParamFromStep(a, MOOG_FX_KIND_COUNT));
            probe.setParameter(MOOG_FX_SLOT_B, moogParamFromStep(b, MOOG_FX_KIND_COUNT));

            probe.setParameter(MOOG_CHORUS_MIX,   1.0f);
            probe.setParameter(MOOG_CHORUS_FB,    1.0f);
            probe.setParameter(MOOG_CHORUS_DEPTH, 1.0f);
            probe.setParameter(MOOG_DELAY_MIX,    1.0f);
            probe.setParameter(MOOG_DELAY_FB,     1.0f);
            probe.setParameter(MOOG_DELAY_TIME,   0.1f);   // short: many passes
            probe.setParameter(MOOG_REVERB_MIX,   1.0f);
            probe.setParameter(MOOG_REVERB_SIZE,  1.0f);
            probe.setParameter(MOOG_REVERB_DAMP,  0.0f);   // no damping at all

            float peak = 0.0f; int nan = 0;
            probe.noteOn(48, 127);
            for (int done = 0; done < 22050; done += 64) probe.processFloat(l, r, 64);
            probe.noteOff(48);

            // 20 s of tail: enough for a slow divergence to become obvious
            for (int done = 0; done < 44100 * 20; done += 64) {
                probe.processFloat(l, r, 64);
                for (int i = 0; i < 64; ++i) {
                    if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { nan++; continue; }
                    peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
                }
            }

            const bool fail = nan || peak > 2.0f;
            if (fail || peak > 0.001f)
                std::printf("  %-7s -> %-7s tail peak %6.3f nan %d%s\n",
                            kindName[a], kindName[b], peak, nan,
                            fail ? "   <-- FAIL" : "");
            if (fail) bad++;
        }
    }

    std::printf("%s\n", bad ? "SELF TEST FAILED" : "self test passed");
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    synth = new Moog();
    synth->setSampleRate((float) HOST_SR);
    synth->setProgram(0);

    // Non-interactive: no audio device, no MIDI, no terminal. This is the
    // form CI runs, and the form to reach for when something sounds wrong and
    // the question is whether the engine or the wiring is at fault.
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

    std::cout << "Moog Minimoog Model D -- host test  (SR=" << HOST_SR
              << ", block=" << I2S_BUFFER_WORDS
              << ", " << MOOG_OVERSAMPLE << "x oversampled, monophonic)\n";
    std::cout << "MIDI: send to the virtual port \"" << VIRT_NAME << "\".\n";
    printStatus();

    std::thread inputThread(inputThreadFunc);
    printf("+/- preset | 1/2/3 waveform | q/w/e range | f/F cutoff | r/R emphasis\n"
           "c/C contour | g glide switch | [/] glide time | o osc mod | k osc3 kbd\n"
           "j filter mod | </> mod wheel | d note | a arpeggio | s sustain\n"
           "p panel | m cc map | t self test | x quit\n"
           "\n"
           "Glide only shows up between two notes: hold 'd', change the preset or\n"
           "play two notes over MIDI. A single repeated note has nothing to glide to.\n");

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

            case '1': cycleEnum(MOOG_OSC1_WAVE);  break;
            case '2': cycleEnum(MOOG_OSC2_WAVE);  break;
            case '3': cycleEnum(MOOG_OSC3_WAVE);  break;
            case 'q': cycleEnum(MOOG_OSC1_RANGE); break;
            case 'w': cycleEnum(MOOG_OSC2_RANGE); break;
            case 'e': cycleEnum(MOOG_OSC3_RANGE); break;

            case 'f': nudge(MOOG_CUTOFF,      -0.03f); break;
            case 'F': nudge(MOOG_CUTOFF,      +0.03f); break;
            case 'r': nudge(MOOG_EMPHASIS,    -0.03f); break;
            case 'R': nudge(MOOG_EMPHASIS,    +0.03f); break;
            case 'c': nudge(MOOG_CONTOUR_AMT, -0.03f); break;
            case 'C': nudge(MOOG_CONTOUR_AMT, +0.03f); break;
            case '<': nudge(MOOG_MOD_WHEEL,   -0.05f); break;
            case '>': nudge(MOOG_MOD_WHEEL,   +0.05f); break;

            case 'g': cycleEnum(MOOG_GLIDE_ON); reportGlide(); break;
            case '[': nudge(MOOG_GLIDE, -0.03f); reportGlide(); break;
            case ']': nudge(MOOG_GLIDE, +0.03f); reportGlide(); break;
            case 'o': cycleEnum(MOOG_OSC_MOD);    break;
            case 'k': cycleEnum(MOOG_OSC3_CTRL);  break;
            case 'j': cycleEnum(MOOG_FILTER_MOD); break;

            case 'd': toggleNote(); break;
            case 'a': arpeggio();   break;
            case 'p': printParams();break;
            case 'm': printCcMap(); break;
            case 't': selfTest();   break;

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
