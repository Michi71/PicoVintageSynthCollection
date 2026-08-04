// RDX_Types.h
#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

// enums RDX_FreqMode, RDX_FeedbackType, RDX_ScaleCurve, RDX_Mode (unveraendert lassen)
enum RDX_FreqMode : uint8_t { RDX_FREQ_RATIO = 0, RDX_FREQ_FIXED = 1 };
enum RDX_FeedbackType : uint8_t { RDX_FB_SAW = 0, RDX_FB_SQUARE = 1 };
enum RDX_ScaleCurve : uint8_t { RDX_SCALE_NEG_LIN = 0, RDX_SCALE_NEG_EXP = 1, RDX_SCALE_POS_EXP = 2, RDX_SCALE_POS_LIN = 3 };
enum RDX_Mode : uint8_t { RDX_MODE_POLY = 0, RDX_MODE_MONO_FULL = 1, RDX_MODE_MONO_LEGATO = 2 };

inline uint8_t rdxSyxChecksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) sum += data[i];
    return (128 - (sum & 0x7F)) & 0x7F;
}
inline uint8_t rdxSyxChecksum(std::initializer_list<uint8_t> list) {
    uint32_t sum = 0;
    for (auto v : list) sum += v;
    return (128 - (sum & 0x7F)) & 0x7F;
}

struct RDX_Controls {
    int pitchbend = 0; float pitchbendSemitones = 0.0f;
    int mainVolume = 100; float mainVolumeFactor = 1.0f;
    int expression = 127; float expressionFactor = 1.0f;
    float tuningSemitones = 0.0f;
    int modWheel = 0; float modWheelFactor = 0.0f;
    bool sustain = false; bool portamento = false; float portaTimeS = 0.06f;
    uint32_t bankMSB = 0, bankLSB = 0, program = 0;
    uint32_t wantBankMSB = 0, wantBankLSB = 0, wantProgram = 0;
    struct ParamPair { uint8_t msb = 0x7F, lsb = 0x7F; };
    ParamPair rpn, nrpn;
    inline uint16_t getBank() const { return (bankMSB << 7) | bankLSB; }
    inline uint16_t getWantBank() const { return (wantBankMSB << 7) | wantBankLSB; }
    inline void setBank(uint16_t bank) { bankMSB = 0b01111111 & (bank >> 7); bankLSB = 0b01111111 & bank; }
};

struct __attribute__((packed)) RDX_System {
    uint8_t txMidiChan = 0; uint8_t rxMidiChan = 0; uint8_t masterTune[4] = {0};
    uint8_t localControl = 1; uint8_t masterTranspose = 0x40; uint8_t tempo[2] = {0, 0x78};
    uint8_t lcdContrast = 20; uint8_t pedalModel = 1; uint8_t autoPowerOff = 1;
    uint8_t speakerOn = 1; uint8_t midiControl = 1; uint8_t reserved[17];
};

struct __attribute__((packed)) RDX_Common {
    uint8_t voiceName[10]; uint8_t reserved1[2]; uint8_t transpose; uint8_t monoPoly;
    uint8_t portaTime; uint8_t pbRange; uint8_t algorithm; uint8_t lfoWave; uint8_t lfoSpeed;
    uint8_t lfoDelay; uint8_t lfoPMD; uint8_t pegRate[4]; uint8_t pegLevel[4];
    uint8_t effects[2][3]; uint8_t reserved2[3];
};

struct __attribute__((packed)) RDX_OpParams {
    uint8_t enable; uint8_t egRate[4]; uint8_t egLevel[4]; uint8_t rateScaling;
    uint8_t scaleLD, scaleRD, scaleLC, scaleRC; uint8_t lfoAMD; uint8_t lfoPMDEnable;
    uint8_t pegEnable; uint8_t velSens; uint8_t outLevel; uint8_t feedback; uint8_t fbType;
    uint8_t freqMode; uint8_t freqCoarse; uint8_t freqFine; uint8_t freqDetune; uint8_t reserved[3];
};

struct __attribute__((packed)) RDX_Patch { RDX_Common common; RDX_OpParams ops[4]; };
struct RDX_Bank { RDX_Patch patches[32]; };
struct SynthState { RDX_System system; RDX_Patch storedPatch; RDX_Patch workingPatch; RDX_Controls controls; };

static inline bool patchToSyx(RDX_Patch& patch, uint8_t* out, uint32_t& outLen, uint8_t midiCh=0, uint8_t patchNum=0) {
    const uint8_t head[] = {0xF0, 0x43, midiCh, 0x7F, 0x1C, 0x00};
    uint8_t* w = out;
    auto writeBlock = [&](uint8_t cmd, const uint8_t* data, uint32_t len) {
        memcpy(w, head, sizeof(head)); w += sizeof(head);
        *w++ = cmd; memcpy(w, data, len); w += len;
        *w++ = rdxSyxChecksum(data, len); *w++ = 0xF7;
    };
    uint8_t a = (patchNum==0) ? 0x0F : 0x00;
    uint8_t p = (patchNum==0) ? 0x00 : (uint8_t)((patchNum-1)%32);
    uint8_t headerBlk[] = {0x05, 0x0E, a, p}; writeBlock(0x04, headerBlk, sizeof(headerBlk));
    uint8_t commonBlk[4+38]; commonBlk[0]=0x05; commonBlk[1]=0x30; commonBlk[2]=0x00; commonBlk[3]=0x00;
    memcpy(commonBlk+4, &patch.common, 38); writeBlock(0x2A, commonBlk, sizeof(commonBlk));
    for (int i=0;i<4;i++) { uint8_t opBlk[4+28]; opBlk[0]=0x05; opBlk[1]=0x31; opBlk[2]=i; opBlk[3]=0x00;
        memcpy(opBlk+4, &patch.ops[i], 28); writeBlock(0x20, opBlk, sizeof(opBlk)); }
    uint8_t footerBlk[] = {0x05, 0x0F, a, p}; writeBlock(0x04, footerBlk, sizeof(footerBlk));
    outLen = (uint32_t)(w - out); return true;
}

static inline bool syxToPatch(const uint8_t* syx, uint32_t len, RDX_Patch& patch) {
    uint32_t i=0;
    while (i+10 < len) {
        if (syx[i]!=0xF0 || syx[i+1]!=0x43) { i++; continue; }
        uint8_t cmd = syx[i+6]; const uint8_t* data = syx+i+7; uint32_t dlen = 0;
        if (cmd==0x2A && data[1]==0x30) { dlen = 42;
            if (rdxSyxChecksum(data, dlen)!=syx[i+7+dlen]) return false;
            memcpy(&patch.common, data+4, 38); }
        else if (cmd==0x20 && data[1]==0x31) { dlen = 32; int op = data[2];
            if (op<0 || op>3) return false;
            if (rdxSyxChecksum(data, dlen)!=syx[i+7+dlen]) return false;
            memcpy(&patch.ops[op], data+4, 28); }
        while (i < len && syx[i]!=0xF7) i++;
        i++;
    }
    return true;
}
