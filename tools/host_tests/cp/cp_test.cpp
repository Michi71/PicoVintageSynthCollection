/*
  test.cpp  --  macOS-Host-Test fuer die mdaEPiano-Engine (PicoMDAEPiano)

  Spielt die (unveraenderte) Pico-mdaEPiano-Engine auf dem Mac ueber CoreAudio
  und steuert sie per MIDI (PortMidi virtueller Input "mdaepiano"). Die
  Instrumente (0..5) sind in die Engine integriert (setInstrument).

  Steuerung:
    + / -   Programm (Preset) wechseln
    1..5    Programm direkt waehlen
    i       Instrument weiter (0=Rd I, 1=Rd II, 2=Wr, 3=Clv, 4=Pno, 5=CP)
    s       Sustain-Pedal (CC64) an/aus
    m       Mod-Wheel (CC1) auf 127
    q       Quit

  Build:  ./test/build.sh
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

#include "mdaEPiano.h"
#include "cp_audio.h"

static const uint32_t HOST_SR      = 44100;
#undef NVOICES
static const uint32_t HOST_NVOICES = 32;
static mdaEPiano* synth = nullptr;
static RefaceCpChain g_fx;
static std::atomic<bool> g_fxReady{false};

// ---------------------------------------------------------------------------
// Audio (CoreAudio) -- Ringpuffer: 16er-Engine-Bloecke verlustfrei gefuellt
// ---------------------------------------------------------------------------
static AudioUnit audioUnit;
static int16_t ringL[I2S_BUFFER_WORDS], ringR[I2S_BUFFER_WORDS];
static UInt32 ringFill = 0, ringPos = 0;

static OSStatus renderCallback(void*, AudioUnitRenderActionFlags*,
                               const AudioTimeStamp*, UInt32,
                               UInt32 inNumberFrames, AudioBufferList* ioData) {
    float* out = reinterpret_cast<float*>(ioData->mBuffers[0].mData);
    for (UInt32 n = 0; n < inNumberFrames; ++n) {
        if (ringPos >= ringFill) { synth->process(ringR, ringL); if (g_fxReady.load()) cp_process_block_i16(g_fx, ringL, ringR, I2S_BUFFER_WORDS); ringPos = 0; ringFill = I2S_BUFFER_WORDS; }
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
static const char* VIRT_NAME = "mdaepiano";
void sendMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    switch (status & 0xF0) {
        case 0x80: synth->noteOff(d1); break;
        case 0x90: (d2 > 0) ? synth->noteOn(d1, d2) : synth->noteOff(d1); break;
        case 0xB0: synth->processMidiController(d1, d2); break;
        case 0xC0: if (d1 < synth->getProgramCount()) synth->setProgram(d1); break;
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
std::atomic<char> lastKey = 0;
char getch() {
    struct termios oldt, newt; tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt; newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return c;
}
void inputThreadFunc() { while (true) { char c = getch(); lastKey = c; if (c == 'q') break; } }

// ---------------------------------------------------------------------------
static void printStatus() {
    char iname[8] = {0}; strncpy(iname, synth->getCurrentInstrumentName(), 7);
    char pname[24] = {0}; synth->getProgramName(pname);
    std::cout << "Instrument " << synth->getCurrentInstrument() << " [" << iname << "]  Programm "
              << synth->getProgram() << "/" << synth->getProgramCount() << ": " << pname << std::endl;
}
// Instrument sichere umschalten (Audio kurz stoppen -> kein Race mit process())
static void nextInstrument() {
    AudioOutputUnitStop(audioUnit);
    synth->setInstrument((synth->getCurrentInstrument() + 1) % MDA_NINSTR);
    AudioOutputUnitStart(audioUnit);
    printStatus();
    g_fx.setVoiceType(synth->getCurrentInstrument());
}

int main() {
    synth = new mdaEPiano(HOST_NVOICES);
    synth->setSampleRate((float)HOST_SR);
    synth->setVolume(100);
    synth->setProgram(0);
    synth->setInstrument(0);   // Rd I (mda-Default)

    // Reface CP effect chain: init + default preset
    g_fx.init((float)HOST_SR);
    g_fx.reset();
    g_fx.setVoiceType(synth->getCurrentInstrument());
    g_fx.setVolume(0.9f);
    g_fx.setDrive(0.15f);
    g_fx.setTremWahMode(RefaceCpChain::TW_OFF);
    g_fx.setChoPhaMode(RefaceCpChain::CP_CHORUS);
    g_fx.setChoPhaDepth(0.4f);
    g_fx.setChoPhaSpeed(0.3f);
    g_fx.setDelayMode(RefaceCpChain::DLY_OFF);
    g_fx.setReverbDepth(0.25f);
    g_fxReady = true;

    if (!initAudio()) { fprintf(stderr, "Audio init failed.\n"); return 1; }
    if (!initMIDI())  { fprintf(stderr, "MIDI init failed (weiter ohne MIDI).\n"); }
    AudioOutputUnitStart(audioUnit);

    std::cout << "mdaEPiano Host-Test  (SR=" << HOST_SR << ", Poly=" << HOST_NVOICES
              << ", Instrumente=" << MDA_NINSTR << ")\n";
    std::cout << "MIDI: virtuellen Port \"" << VIRT_NAME << "\" ansteuern.\n";
    printStatus();
    std::thread inputThread(inputThreadFunc);
    printf("+/- = Programm, 1..5 = direkt, i = Instrument, s = Sustain, m = ModWheel, q = Quit\n");
    printf("  Reface CP FX:  t = Trem/Wah | c = Cho/Pha | d = Delay | r = Reverb\n");

    bool sustainToggle = false, quit = false;
    while (!quit) {
        updateMIDI();
        char c = lastKey.exchange(0);
        switch (c) {
            case '+': { int p=(synth->getProgram()+1)%synth->getProgramCount(); synth->setProgram(p); printStatus(); break; }
            case '-': { int p=synth->getProgram()-1; if(p<0)p=synth->getProgramCount()-1; synth->setProgram(p); printStatus(); break; }
            case '1': case '2': case '3': case '4': case '5': synth->setProgram(c-'1'); printStatus(); break;
            case 'i': nextInstrument(); break;
            case 's': sustainToggle=!sustainToggle; synth->processMidiController(0x40, sustainToggle?127:0);
                      std::cout << "Sustain " << (sustainToggle?"AN":"AUS") << std::endl; break;
            case 'm': synth->processMidiController(0x01, 127); std::cout << "ModWheel 127\n"; break;
            case 't': { int m=g_fx.getTremWahMode(); g_fx.setTremWahMode((m+1)%3);
                        printf("Trem/Wah: %d (0=off 1=trem 2=wah)\n", g_fx.getTremWahMode()); break; }
            case 'c': { int m=g_fx.getChoPhaMode(); g_fx.setChoPhaMode((m+1)%3);
                        printf("Cho/Pha: %d (0=off 1=chorus 2=phaser)\n", g_fx.getChoPhaMode()); break; }
            case 'd': { int m=g_fx.getDelayMode(); g_fx.setDelayMode((m+1)%3);
                        printf("Delay: %d (0=off 1=digital 2=analog)\n", g_fx.getDelayMode()); break; }
            case 'r': { float dep = g_fx.getReverbDepth() > 0.01f ? 0.0f : 0.3f; g_fx.setReverbDepth(dep);
                        printf("Reverb depth: %.2f\n", dep); break; }
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
