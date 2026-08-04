/*
  solina_test.cpp  --  macOS host test for the Solina engine (PicoFaceSM)

  Plays the (unmodified) Pico Solina engine from include/solina + src/solina
  on the Mac through CoreAudio and drives it over MIDI (PortMidi virtual
  input "solina").

  Controls:
    + / -   change program (preset)
    1..8    select a program directly
    Registers (Tone Section):
      y     Contrabass 16'      x   Cello 8'
      c     Viola 8'            v   Violin 4'
      b     Trumpet 8'          n   Horn 4'
    e       Ensemble (modulation) on/off
    s       sustain pedal (CC64) on/off
    d       play / release a demo chord
    a       arpeggio across the keyboard
    p       print the parameter list
    q       quit

  Build:  ./test/build_solina.sh
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

#include "solina/solina.h"

static const uint32_t HOST_SR = 44100;
static Solina* synth = nullptr;

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
static const char* VIRT_NAME = "solina";
void sendMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    switch (status & 0xF0) {
        case 0x80: synth->noteOff(d1); break;
        case 0x90: (d2 > 0) ? synth->noteOn(d1, d2) : synth->noteOff(d1); break;
        case 0xB0: synth->processMidiController(d1, d2); break;
        case 0xC0: if (d1 < synth->getProgramCount()) synth->setProgram(d1); break;
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
void inputThreadFunc() { while (true) { char c = getch(); lastKey = c; if (c == 'q') break; } }

// ---------------------------------------------------------------------------
static const char* onoff(int32_t p) {
    return synth->getParameter(p) != 0.0f ? "*" : "-";
}
static void printStatus() {
    char pname[24] = {0}; synth->getProgramName(pname);
    std::printf("Program %d/%d: %-16s  [CB %s Cel %s Vla %s Vln %s Tpt %s Hrn %s]  Ensemble %s\n",
                synth->getProgram() + 1, synth->getProgramCount(), pname,
                onoff(SOLINA_CONTRABASS), onoff(SOLINA_CELLO),
                onoff(SOLINA_VIOLA), onoff(SOLINA_VIOLIN),
                onoff(SOLINA_TRUMPET), onoff(SOLINA_HORN),
                synth->getParameter(SOLINA_ENSEMBLE) != 0.0f ? "on" : "off");
}

static void printParams() {
    char n[24], d[16], l[16];
    std::cout << "--- Parameters --------------------------------------\n";
    for (int32_t i = 0; i < synth->getParameterCount(); i++) {
        synth->getParameterName(i, n);
        synth->getParameterDisplay(i, d);
        synth->getParameterLabel(i, l);
        std::printf("  %2d  %-12s %8s %s\n", (int) i, n, d, l);
    }
}

static void toggle(int32_t param) {
    synth->setParameter(param, synth->getParameter(param) != 0.0f ? 0.0f : 1.0f);
    printStatus();
}

static bool chordDown = false;
static const int chord[4] = { 48, 55, 64, 67 };
static void toggleChord() {
    for (int k : chord) chordDown ? synth->noteOff(k) : synth->noteOn(k, 100);
    chordDown = !chordDown;
    std::cout << (chordDown ? "Chord on\n" : "Chord off\n");
}

// Arpeggio across the keyboard range -- shows the keyboard split of the timbre
static void arpeggio() {
    std::cout << "Arpeggio C2..C6\n";
    for (int n = 36; n <= 84; n += 2) {
        synth->noteOn(n, 100);
        usleep(90000);
        synth->noteOff(n);
    }
}

int main() {
    synth = new Solina();
    synth->setSampleRate((float) HOST_SR);
    synth->setVolume(100);
    synth->setProgram(2);

    if (!initAudio()) { fprintf(stderr, "Audio init failed.\n"); return 1; }
    if (!initMIDI())  { fprintf(stderr, "MIDI init failed (continuing without MIDI).\n"); }
    AudioOutputUnitStart(audioUnit);

    std::cout << "ARP Solina String Ensemble -- host test  (SR=" << HOST_SR
              << ", block=" << I2S_BUFFER_WORDS << ", keyboard C2..C6)\n";
    std::cout << "MIDI: send to the virtual port \"" << VIRT_NAME << "\".\n";
    printStatus();
    std::thread inputThread(inputThreadFunc);
    printf("+/- program | 1..8 direct | y/x bass | c/v strings | b/n brass\n"
           "e ensemble | s sustain | d chord | a arpeggio | p params | q quit\n");

    bool sustainToggle = false, quit = false;
    while (!quit) {
        updateMIDI();
        char c = lastKey.exchange(0);
        switch (c) {
            case '+': { int p=(synth->getProgram()+1)%synth->getProgramCount(); synth->setProgram(p); printStatus(); break; }
            case '-': { int p=synth->getProgram()-1; if(p<0)p=synth->getProgramCount()-1; synth->setProgram(p); printStatus(); break; }
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8':
                synth->setProgram(c-'1'); printStatus(); break;
            case 'y': toggle(SOLINA_CONTRABASS); break;
            case 'x': toggle(SOLINA_CELLO);      break;
            case 'c': toggle(SOLINA_VIOLA);      break;
            case 'v': toggle(SOLINA_VIOLIN);     break;
            case 'b': toggle(SOLINA_TRUMPET);    break;
            case 'n': toggle(SOLINA_HORN);       break;
            case 'e': toggle(SOLINA_ENSEMBLE);   break;
            case 'd': toggleChord(); break;
            case 'a': arpeggio(); break;
            case 'p': printParams(); break;
            case 's': sustainToggle=!sustainToggle;
                      synth->processMidiController(0x40, sustainToggle?127:0);
                      std::cout << "Sustain " << (sustainToggle?"ON":"OFF") << std::endl; break;
            case 'q': quit = true; break;
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
