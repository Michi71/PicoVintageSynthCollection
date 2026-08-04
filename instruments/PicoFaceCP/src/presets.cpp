/*
  presets.cpp -- Factory preset table + apply logic.

  engine[12] layout = mda params: 0 Decay, 1 Release, 2 Hardness, 3 Treble,
  4 Modulation (neutral 0.5), 5 LFO Rate (neutral 0.65), 6 VelSense,
  7 StereoWidth, 8 Poly, 9 FineTune, 10 RandomTune, 11 Overdrive (neutral 0.0).
  Params 4/5/11 are neutralised in the engine (FX chain is authoritative).
*/
#include "presets.h"
#include "mdaEPiano.h"
#include "reface_cp_chain.h"

const CpPreset cpPresets[CP_NPRESETS] = {
    // 0 Rd I Classic
    { "Rd I Classic", 0,
      { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      0, 0, 0,
      0.0f, 0.0f, 0.0f, 0.4f, 0.3f, 0.0f, 0.0f, 0.15f },

    // 1 Rd II Chorus
    { "Rd II Chorus", 1,
      { 0.5f, 0.5f, 0.5f, 0.60f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      0, 1, 0,
      0.0f, 0.0f, 0.0f, 0.45f, 0.30f, 0.0f, 0.0f, 0.20f },

    // 2 Phaser Rd
    { "Phaser Rd", 1,
      { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      0, 2, 0,
      0.0f, 0.0f, 0.0f, 0.60f, 0.35f, 0.0f, 0.0f, 0.20f },

    // 3 Wurli Trem
    { "Wurli Trem", 2,
      { 0.5f, 0.5f, 0.55f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      1, 0, 0,
      0.20f, 0.55f, 0.50f, 0.4f, 0.3f, 0.0f, 0.0f, 0.15f },

    // 4 Funky Clv
    { "Funky Clv", 3,
      { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      2, 0, 0,
      0.0f, 0.70f, 0.50f, 0.4f, 0.3f, 0.0f, 0.0f, 0.10f },

    // 5 Piano Hall
    { "Piano Hall", 4,
      { 0.5f, 0.60f, 0.5f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      0, 0, 0,
      0.0f, 0.0f, 0.0f, 0.4f, 0.3f, 0.0f, 0.0f, 0.45f },

    // 6 CP Delay
    { "CP Delay", 5,
      { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      0, 0, 2,
      0.15f, 0.0f, 0.0f, 0.4f, 0.3f, 0.35f, 0.45f, 0.20f },

    // 7 Space Rd
    { "Space Rd", 0,
      { 0.5f, 0.60f, 0.5f, 0.5f, 0.5f, 0.65f, 0.25f, 0.5f, 1.0f, 0.5f, 0.146f, 0.0f },
      0, 1, 1,
      0.0f, 0.0f, 0.0f, 0.50f, 0.30f, 0.30f, 0.50f, 0.40f },
};

void preset_apply(uint8_t idx, mdaEPiano* ep, RefaceCpChain* fx)
{
    if (idx >= CP_NPRESETS) return;
    const CpPreset& p = cpPresets[idx];

    ep->setInstrument(p.instrument);
    fx->setVoiceType(ep->getCurrentInstrument());
    for (int i = 0; i < 12; i++) ep->setParameter(i, p.engine[i]);
    fx->setTremWahMode(p.twMode);
    fx->setChoPhaMode(p.cpMode);
    fx->setDelayMode(p.dlyMode);
    fx->setDrive(p.drive);
    fx->setTremWahDepth(p.twDepth);
    fx->setTremWahRate(p.twRate);
    fx->setChoPhaDepth(p.cpDepth);
    fx->setChoPhaSpeed(p.cpSpeed);
    fx->setDelayDepth(p.dlyDepth);
    fx->setDelayTime(p.dlyTime);
    fx->setReverbDepth(p.reverb);
}

static uint8_t s_current = 0;

void preset_set_current(uint8_t idx)
{
    s_current = idx;
}

uint8_t preset_get_current(void)
{
    return s_current;
}
