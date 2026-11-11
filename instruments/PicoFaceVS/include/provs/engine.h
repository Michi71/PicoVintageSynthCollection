// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// ============================================================================
//  provs/engine.h — Prophet-VS-Engine: Original-Architektur (Wellen, Vektor-
//  Mix, 5-Punkt-Envelopes, LFOs, Modulationsmatrix, analoge Filterstufe)
//  + bandbegrenzte Mip-Wellen und Effekte (Chorus, Ensemble, VS-Chorus).
//
//  "Translation, not emulation": native C++17, portabel (RP2350-tauglich),
//  blockbasiert, keine OS-Abhaengigkeiten.
//
//  Architektur je Stimme (wie das Original):
//    4 x Wavetable-OSZ (Welle 0..127, Coarse 0..24 Halbtoene, Fine 0..99 Cent)
//    -> Vektor-Mix (Raute A/B/C/D) mit Mix-Envelope (5 Punkte X/Y)
//    -> 4-Pol-Tiefpass (Cutoff, Resonanz, Filter-Envelope)
//    -> VCA mit Amp-Envelope -> Pan
//    -> Summe -> Effekte (provs::Fx) -> Ausgang
//
//  Tonhoehe wie in der Firmware (Routine @0x5D12): alles in ganzen Cent,
//    cent = (Note - 27)*100 + Coarse*100 + Fine
//  und daraus exponentiell (Tabelle @0x8000, 1200 Eintraege je Oktave).
//  Die Engine verankert das auf A4 = 440 Hz bei Coarse 0 / Fine 0.
//
//  Modulation wie in der Timer-ISR der Firmware (@0x8DC ff.): zwei globale
//  LFOs und die Quellen Velocity, Keyboard, Filter-Envelope und Mod-Wheel
//  werden ueber Ziel-Bitmasken auf Tonhoehe, Filter, Mix, Pan, Pegel und die
//  LFOs selbst geschaltet. Skalierungen stehen bei den Feldern.
// ============================================================================
#pragma once
#include <cstddef>
#include <cstdint>

#include "provs/fx.h"
#include "provs/config.h"
#include "provs/gen/wavetables.h"

namespace provs {

constexpr int kMaxVoices = 8;   // wie das Original
constexpr int kNoiseWave = 127; // Welle 127 = Rauschen (Chip-Flag im Original)
constexpr int kControlSamples = 32;   // Modulation alle 32 Samples neu rechnen

// Filterkennlinie des Originals (Schaltplan SD2400-4, Datenblatt CEM3379):
//   Vpin = CV * kVcfSpanMv - kVcfOffsetMv,  fc = kVcfHzAt0 * exp(Vpin / kVcfVtMv)
// CV 0..1 ist die DAC-Spannung 0..5,2 V; sie liegt ueber 20k + 1k am
// Frequenzeingang, mit 1k nach Masse und 75k nach -8 V. Der Baustein rechnet
// fc = Gm / (2 pi C), Gm = 200 uS * exp(Vpin / VT), C = 33 nF.
// VT ist die Temperaturspannung der Steuerkennlinie: 28,5 mV bei 20 Grad C
// (Datenblatt), sie waechst mit der absoluten Temperatur und ist im VS nicht
// kompensiert (1 kOhm Metallfilm statt Tempco-Widerstand). Mit 30,9 mV
// (etwa 45 Grad C im Geraet) treffen alle drei Angaben des Handbuchs zu:
// elf Oktaven Regelbereich, rund 9 Einheiten je Oktave und exaktes
// Key-Tracking bei Keyboard-Betrag nahe 50.
constexpr float kVcfSpanMv = 233.4f;
constexpr float kVcfOffsetMv = 100.5f;
constexpr float kVcfHzAt0 = 964.6f;
constexpr float kVcfVtMv = 30.9f;

// ---------------------------------------------------------------------------
// VS-Envelope (Handbuch Kap. 8): startet auf L0, laeuft linear
// L0->L1->L2->L3, haelt auf L3 solange die Taste gedrueckt ist und faellt
// danach exponentiell auf L4.
// Loop wie in der Firmware (@0x4756, @0x0A0C):
//   0      kein Loop
//   1..3   bei Punkt 3 Sprung zurueck auf Punkt 0/1/2, dann wieder vorwaerts
//   4..6   pendelt zwischen Punkt 0/1/2 und Punkt 3 (rueckwaerts mit denselben
//          Segmentzeiten)
// repeat: 0 = endlos, n = n Wiederholungen, danach halten.
struct Env5 {
    float level[5] = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    float time[3]  = {0.008f, 0.008f, 0.008f};   // Segmentdauern in Sekunden
    float releaseTau = 0.0f;                     // Zeitkonstante, 0 = sofort
    float releaseTauAlt = -1.0f;                 // Rate 4A (Alternate Release), < 0 = keine
    // > 0: Segmente laufen nicht linear, sondern wie 1 - exp(-curve * t/T)
    // auf ihr Ziel zu (Kurvenform der Behringer-Envelopes)
    float curve = 0.0f;
    int loop = 0, repeat = 0;
};

struct OscParams {
    int   wave = 32;         // 0..127 (Original-Wellennummer; 32 = Sinus)
    int   coarse = 0;        // 0..24 Halbtoene aufwaerts
    int   fine = 0;          // 0..99 Cent aufwaerts
    float level = 1.0f;
};

// Ziel-Bits der LFOs (Reihenfolge der Firmware-Tabellen @0x5F6 / @0x61E)
enum LfoDest : uint16_t {
    kLfoFreqA = 1 << 0, kLfoFreqB = 1 << 1, kLfoFreqC = 1 << 2, kLfoFreqD = 1 << 3,
    kLfoFilter = 1 << 4,       // +-1/4 der Cutoff-Skala bei Betrag 99
    kLfoMixAC = 1 << 5,        // Mix X, volle Skala
    kLfoMixBD = 1 << 6,        // Mix Y, volle Skala
    kLfoOtherRate = 1 << 7,    // Rate des jeweils anderen LFO
    kLfoOtherAmount = 1 << 8,  // Betrag des jeweils anderen LFO
    kLfoPan = 1 << 9,
};
enum LfoShape { kLfoTriangle = 0, kLfoSquare, kLfoSaw, kLfoRamp, kLfoRandom };

struct LfoParams {
    int shape = kLfoTriangle;
    int rate = 70;             // 0..99, Tabelle kLfoRateStep
    float hz = 0.0f;           // > 0: Dreieck-Frequenz direkt, statt der Tabelle
    int amount = 0;            // 0..99; 99 = +-256 Cent auf die Tonhoehe
    uint16_t dest = 0;         // LfoDest-Bits
};

// Ziel-Bits der uebrigen Quellen (Firmware @0x5AE0 ff., @0x45CA, @0x45F8)
enum : uint8_t {
    kVelFilterEnv = 1, kVelMixAC = 2, kVelMixBD = 4, kVelAmp = 8, kVelPan = 16,
    kKeyFilter = 1, kKeyMixAC = 2, kKeyMixBD = 4, kKeyPan = 8,
    kFenvFreq = 1, kFenvPan = 2,
    kWheelLfo1 = 1, kWheelLfo2 = 2, kWheelChorus = 4,
};
// Ziel-Bits der Pressure-Quelle (Firmware-Tabelle @0x6E6)
enum PressDest : uint16_t {
    kPressFreqA = 1 << 0, kPressFreqB = 1 << 1, kPressFreqC = 1 << 2, kPressFreqD = 1 << 3,
    kPressFilter = 1 << 4, kPressMixAC = 1 << 5, kPressMixBD = 1 << 6,
    kPressLfo1Rate = 1 << 7, kPressLfo1Amount = 1 << 8,
    kPressLfo2Rate = 1 << 9, kPressLfo2Amount = 1 << 10,
    kPressVolume = 1 << 11, kPressPan = 1 << 12,
    kPressChorusRate = 1 << 13, kPressChorusDepth = 1 << 14,
};

enum KeyMode { kKeyNormal = 0, kKeySplit = 1, kKeyDouble = 2 };
enum ArpMode { kArpNormal = 0, kArpAutoLatch = 1, kArpExtend = 2 };
enum ArpScan { kArpUp = 0, kArpDown, kArpUpDown, kArpAssign, kArpReverse, kArpRandom };

struct Params {
    OscParams osc[4] = {{32}, {33}, {34}, {35}};   // A B C D (Init-Patch der Firmware)
    Env5 ampEnv;
    Env5 filterEnv = {{0, 0, 0, 0, 0}};
    // Mix-Envelope: 5 Punkte in Joystick-Koordinaten [-1,1] (0 = Mitte).
    // A liegt bei X = -1, C bei X = +1, B bei Y = -1, D bei Y = +1.
    Env5 mixEnvX = {{0, 0, 0, 0, 0}};
    Env5 mixEnvY = {{0, 0, 0, 0, 0}};
    float mixAmount = 1.0f;  // Tiefe der Mix-Envelope (1 = wie programmiert)
    float mixX = 0.0f, mixY = 0.0f;   // Joystick-Versatz (live)
    float cutoff = 1.0f;     // 0..1, linear in Halbtoenen (wie die CV im Original)
    float resonance = 0.0f;  // 0..1
    float envAmount = 0.0f;  // Filter-Envelope-Tiefe 0..1
    float ampLevel = 1.0f;   // Grundpegel des VCA 0..1 (Firmware-Feld 69)

    LfoParams lfo[2];
    // Quellen mit Vorzeichen: Betrag -99..+99, Ziele als Bitmaske
    int velAmount = 0;   uint8_t velDest = 0;
    int keyAmount = 0;   uint8_t keyDest = 0;    // bezogen auf Note 66
    int fenvAmount = 0;  uint8_t fenvDest = 0;
    int pressAmount = 0; uint16_t pressDest = 0;  // PressDest-Bits
    uint8_t wheelDest = kWheelLfo1;
    float pan[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};   // -1 .. +1 je Stimme

    int glide = 0;           // 0 = aus, 1..99 ueber kGlideRate
    int unison = 0;          // 0 = aus, n = Unisono mit n Cent Versatz je Stimme

    // Tastaturmodus (nur in der Hauptschicht ausgewertet). Bei Split und
    // Double spielt eine zweite Schicht (Engine::setLayer2) auf den Stimmen
    // 5-8, die Hauptschicht auf 1-4.
    int keyMode = kKeyNormal;
    int splitPoint = 60;       // Split: Tasten darunter spielen Schicht 2
    int doubleDetune = 0;      // Double: Schicht 2 um 0..31 Cent hoeher
    float doubleDelay = 0.0f;  // Double: Schicht 2 um so viele Sekunden spaeter

    // Arpeggiator (Firmware-Felder 86-93; Handbuch Kap. 5)
    int arpRate = 70;        // 1..99: Clock-Periode (106 - Rate) ms; 0 = steht
    int arpMode = kArpNormal;
    int arpScan = kArpUp;
    int arpOctaves = 0;      // 0..3 zusaetzliche Oktaven nach oben
    int arpRepeats = 0;      // 0..3 zusaetzliche Wiederholungen je Note
    bool arpVelocity = true; // gespielte Velocity uebernehmen (sonst fest 80)
    int arpSplit = 0;        // nur bei Split: 0 aus, 1 rechts, 2 links arpeggieren
    bool arpPoly = true;     // false: Mono-Voicing, eine Stimme je Schicht
    bool arpRests = false;   // oberstes C (Note 96) zaehlt als Pause

    // Chord-Modus des Pro VS Mini: 0 = aus, 1..8 = Akkordsatz (kChordSets);
    // jede Taste spielt drei Toene. Nur mit den Behringer-Daten (PROVS_HAS_PVM).
    int chordSet = 0;

    int transpose = 0;       // Halbtoene (Behringer: Coarse 12 = Kammerton -> -12)
    float tuneCents = 0.0f;  // Master-Tune
    float bendRange = 2.0f;  // Halbtoene
    float gain = 0.7f;
    Fx::Type fxType = Fx::kOff;
    float fxRate = 0.4f, fxDepth = 0.5f;
    float fxRateHz = 0.0f;   // > 0: Chorus-Frequenz direkt (Original-Programme)
    uint8_t chorusOn = 3;    // Fx::kVsChorus: Bit 0 rechts, Bit 1 links (Feld 66)
};

// ---------------------------------------------------------------------------
class Engine {
public:
    void init(double sampleRate);
    void setParams(const Params& p);
    const Params& params() const { return pNext_; }
    // Zweite Schicht fuer Split/Double (das "verknuepfte Programm"). Global
    // gelten immer die Werte der Hauptschicht: Tastaturmodus, Pan je Stimme,
    // Joystick, Tune, Bend, Gain, FX.
    void setLayer2(const Params& p);
    const Params& layer2() const { return p2Next_; }

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff();
    void pitchBend(float amount);            // -1..+1 (mal bendRange)
    void setModWheel(float amount);          // 0..1 -> LFO-Betrag
    void setPressure(float amount);          // 0..1 (Channel Pressure)
    // Alternate-Release-Fussschalter: Release laeuft mit Rate 4A statt Rate 4
    void setAltRelease(bool on) { altRelease_ = on; }
    void setHold(bool on) { hold_ = on; if (!on) releaseHeld(); }

    // Arpeggiator: eingeschaltet laufen noteOn/noteOff ueber ihn. Die
    // Optionen stehen in Params; Clocks/Step ist wie beim Original global
    // (Werkseinstellung 6).
    void setArp(bool on);
    bool arp() const { return arpOn_; }
    void setArpClocksPerStep(int clocks) { arpClocksPerStep_ = clocks < 1 ? 1 : clocks; }
    // Latch: die gespielten Tasten bleiben nach dem Loslassen gemerkt.
    void setArpLatch(bool on);
    // Transpose der Arpeggiator-Noten in Halbtoenen (im Original: Taste
    // relativ zum mittleren C bei gehaltenem Latch/Transpose)
    void setArpTranspose(int semitones) { arpTranspose_ = semitones; }
    // Externer Takt: bei true laeuft der Arpeggiator nur ueber arpTick()
    // (z. B. MIDI-Clock, 24 je Viertel)
    void setArpExternalClock(bool on) { arpExternal_ = on; }
    void arpTick() { if (arpOn_ && arpExternal_ && arpCount_ > 0) arpClock(); }

    // Audio-Block rendern (interleaved stereo)
    void render(float* out, int frames);

    int activeVoices() const { return active_; }

    // --- Programme -----------------------------------------------------------
    // Die Programmbank liegt im RAM und gehoert allen Engine-Instanzen
    // gemeinsam. Nach dem Start enthaelt sie nur den Init-Patch der Firmware;
    // Programme kommen zur Laufzeit aus SysEx-Daten des Nutzers. Laden nicht
    // gleichzeitig mit render()/setPreset() aus einem anderen Thread.
    //
    // loadVsSysex: SysEx-Dump des Prophet VS, auch mehrere Nachrichten
    // hintereinander. Bank-Dump (F0 01 0A 64, 100 Programme) ersetzt die Bank,
    // Einzelprogramm (F0 01 0A nn) belegt Platz nn. Rueckgabe: Zahl der
    // uebernommenen Programme, 0 = nichts Gueltiges (Bank bleibt unveraendert).
    static int loadVsSysex(const uint8_t* data, size_t size);
    static void resetVsBank();               // zurueck auf den Init-Patch
    static int presetCount();                // Programme in der Bank
    void setPreset(int index);
    // Programmname (8 Zeichen) in buf[9] schreiben
    static void presetName(int index, char* buf);

    // Patchbank des Behringer Pro VS Mini (F0 00 20 32 ..., 32 Patches).
    // Nur mit den optionalen Behringer-Tabellen (PROVS_HAS_PVM, ADSR-Zeiten)
    // nutzbar, sonst Rueckgabe 0.
    static int loadPvmSysex(const uint8_t* data, size_t size);
    static void resetPvmBank();
    static int pvmPatchCount();
    static const char* pvmPatchName(int index);
    void setPvmPatch(int index);

    // Tonhoehe eines Oszillators in Hz ohne Modulation (fuer Tests / Anzeige)
    float oscHz(int note, int osc) const;

private:
    enum { kEnvHold = 6, kEnvRelease = 7, kEnvDone = 8 };
    struct EnvState {
        float v = 0.0f, inc = 0.0f, target = 0.0f, coef = 0.0f;
        int left = 0;
        int repeat = 0;
        int stage = kEnvDone;  // 0..5 Segmente (3..5 = Ruecklauf), dann s. o.
    };
    struct Lfo {
        float phase = 0.0f;    // -1 .. +1 (16-Bit-Phase der Firmware)
        float dir = 1.0f;
        float rnd = 0.0f;
        float out = 0.0f;      // Formwert mal Betrag, -1 .. +1
    };
    struct Voice {
        bool on = false, held = false, pending = false;
        int index = 0;
        int layer = 0;
        int note = 60;
        float velMod = 0.0f, keyMod = 0.0f;
        float detune = 0.0f;   // Unisono-Versatz in Cent
        float glide = 0.0f;    // Rest-Abstand zur Zieltonhoehe in Cent
        float phase[4] = {0, 0, 0, 0};
        float inc[4] = {0, 0, 0, 0};
        const int16_t* tab[4] = {nullptr, nullptr, nullptr, nullptr};
        float tabScale[4] = {0, 0, 0, 0};   // int16 -> Pegel
        float noise[4] = {0, 0, 0, 0};
        EnvState amp, filt, mx, my;
        // im Steuertakt berechnet
        float fcoef = 1.0f, level = 0.0f, panL = 0.5f, panR = 0.5f;
        float modX = 0.0f, modY = 0.0f;
        // Schritt je Sample bis zum naechsten Steuertakt (gegen Treppenstufen):
        // fcoef, level, panL, panR, modX, modY, inc[0..3]
        float ramp[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        float vcaCv = 0.0f;    // geglaettete VCA-Steuerspannung
        float vcfCv = -1.0f;   // geglaettete Filter-Steuerspannung (< 0: noch keine)
        float f0 = 0.0f, f1 = 0.0f, f2 = 0.0f, f3 = 0.0f;
        float dcX = 0, dcY = 0;
        uint64_t age = 0;
    };

    void envSegment(EnvState& e, const Env5& p, int stage) const;
    void envStart(EnvState& e, const Env5& p) const;
    void envRelease(EnvState& e, const Env5& p) const;
    void envTick(EnvState& e, const Env5& p) const;

    struct Delayed { int samples; int note; int velocity; bool on; };

    const Params& lp(int layer) const { return layer ? p2_ : p_; }
    void applyParams();
    void lfoAdvance(int samples, int layer);
    void voiceControl(Voice& v, bool snap = false);
    int pickVoice(int lo, int hi);
    void startVoice(int index, int note, int velocity, float detune, int layer);
    void layerRange(int layer, int& lo, int& hi) const;
    void layerNoteOn(int layer, int note, int velocity);
    void layerNoteOff(int layer, int note);
    void unisonUpdate(int layer, int velocity);
    void queueDelayed(int note, int velocity, bool on);
    void keyOn(int note, int velocity);
    void keyOff(int note);
    void inputOn(int note, int velocity);
    void inputOff(int note);
    bool arpTakes(int note) const;
    void arpClock();
    void arpStep();
    void arpGateOff();
    void arpRestart();
    void startRelease(Voice& v);
    void releaseHeld();
    void countActive();

    float sr_ = 44100.0f;
    Params p_, pNext_;
    Params p2_, p2Next_;       // zweite Schicht
    bool paramsDirty_ = true;
    Voice voices_[kMaxVoices];
    Lfo lfo_[2][2];            // [Schicht][LFO]
    int ctl_ = 0;
    int active_ = 0;
    float bend_ = 0.0f;
    float wheel_ = 0.0f;
    float pressure_ = 0.0f;
    // je Stimme: letzte gespielte Tonhoehe (fuer Glide), ueberlebt Voice-Reset
    float lastCents_[kMaxVoices] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool played_[kMaxVoices] = {false, false, false, false, false, false, false, false};
    // Unisono: gehaltene Tasten, es klingt die tiefste
    int heldNotes_[2][16];
    int heldCount_[2] = {0, 0};
    int unisonNote_[2] = {-1, -1};
    // Arpeggiator
    struct ArpKey { int note; int velocity; };
    static constexpr int kArpMaxKeys = 64;
    ArpKey arpKeys_[kArpMaxKeys];
    int arpCount_ = 0, arpHeld_ = 0;
    bool arpOn_ = false;
    int arpClocksPerStep_ = 6;
    int arpClock_ = 0;             // Clocks bis zum naechsten Schritt
    float arpSamples_ = 0.0f;      // Samples bis zum naechsten Clock
    int arpPos_ = -1, arpOct_ = 0, arpDir_ = 1, arpRepeatLeft_ = 0;
    int arpNote_ = -1, arpVel_ = 80;   // letzte gespielte Note
    bool arpSounding_ = false;
    bool arpLatch_ = false, arpExternal_ = false, arpFromArp_ = false;
    int arpTranspose_ = 0;
    // Double mit Delay: verzoegerte Noten der zweiten Schicht
    Delayed delayed_[32];
    int delayedCount_ = 0;
    bool hold_ = false;
    bool altRelease_ = false;
    uint32_t rng_ = 0x2545F491u;
    Fx fx_;
    uint64_t clock_ = 0;
};

}  // namespace provs
