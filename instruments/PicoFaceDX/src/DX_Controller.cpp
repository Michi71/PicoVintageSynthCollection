#include "DX_Controller.h"
#include "ipc.h"

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Page advance with correct modulo for negative deltas.
// C++ % keeps the sign of the dividend, so we normalize to [0, COUNT).
// ---------------------------------------------------------------------------
DxPage DX_Controller::advancePage(DxPage current, int delta) {
    constexpr int N = static_cast<int>(DxPage::COUNT);
    int idx = static_cast<int>(current) + delta;
    idx = ((idx % N) + N) % N;
    return static_cast<DxPage>(idx);
}

void DX_Controller::onEncoder1(int delta) {
    page_ = advancePage(page_, delta);
}

// ---------------------------------------------------------------------------
// Clamp helper: add delta to value, clamp into [lo, hi].
// Works on uint8_t fields via int intermediate to avoid under/overflow.
// ---------------------------------------------------------------------------
static inline uint8_t clampAdd(uint8_t value, int delta, int lo, int hi) {
    int v = static_cast<int>(value) + delta;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return static_cast<uint8_t>(v);
}

void DX_Controller::onEncoder2(int delta) {
    const RDX_Patch& p = bridge_.patch();  // read-only (cross-core read, matches existing convention)
    static const DxParamId freqParamId[4] = {
        DX_PARAM_OP1_FREQ, DX_PARAM_OP2_FREQ, DX_PARAM_OP3_FREQ, DX_PARAM_OP4_FREQ
    };

    switch (page_) {
        case DxPage::OP1:
        case DxPage::OP2:
        case DxPage::OP3:
        case DxPage::OP4: {
            uint8_t opIdx = static_cast<uint8_t>(page_);
            uint8_t newVal = clampAdd(p.ops[opIdx].freqCoarse, delta, 0, 31);
            ipc_send_dx_param(freqParamId[opIdx], newVal);
            break;
        }
        case DxPage::LFO: {
            uint8_t newVal = clampAdd(p.common.lfoSpeed, delta, 0, 127);
            ipc_send_dx_param(DX_PARAM_LFO_SPEED, newVal);
            break;
        }
        case DxPage::ALGO: {
            uint8_t newVal = clampAdd(p.common.algorithm, delta, 0, 11);
            ipc_send_dx_param(DX_PARAM_ALGO, newVal);
            break;
        }
        case DxPage::FX1:
        case DxPage::FX2: {
            uint8_t slot = (page_ == DxPage::FX1) ? 0 : 1;
            uint8_t offset = (uint8_t)(offsetof(RDX_Common, effects) + slot * 3 + 0);
            uint8_t newVal = clampAdd(p.common.effects[slot][0], delta, 0, 7);
            ipc_send_dx_raw_write(1, offset, newVal);
            break;
        }
        case DxPage::COUNT:
            break;  // unreachable
    }
}

void DX_Controller::onEncoder3(int delta) {
    const RDX_Patch& p = bridge_.patch();  // read-only (cross-core read, matches existing convention)
    static const DxParamId levelParamId[4] = {
        DX_PARAM_OP1_LEVEL, DX_PARAM_OP2_LEVEL, DX_PARAM_OP3_LEVEL, DX_PARAM_OP4_LEVEL
    };

    switch (page_) {
        case DxPage::OP1:
        case DxPage::OP2:
        case DxPage::OP3:
        case DxPage::OP4: {
            uint8_t opIdx = static_cast<uint8_t>(page_);
            uint8_t newVal = clampAdd(p.ops[opIdx].outLevel, delta, 0, 127);
            ipc_send_dx_param(levelParamId[opIdx], newVal);
            break;
        }
        case DxPage::LFO: {
            uint8_t newVal = clampAdd(p.common.lfoPMD, delta, 0, 127);
            ipc_send_dx_param(DX_PARAM_LFO_PMD, newVal);
            break;
        }
        case DxPage::ALGO: {
            // Feedback of Operator 1, tuned alongside algorithm.
            uint8_t newVal = clampAdd(p.ops[0].feedback, delta, 0, 127);
            ipc_send_dx_param(DX_PARAM_OP1_FEEDBACK, newVal);
            break;
        }
        case DxPage::FX1:
        case DxPage::FX2: {
            uint8_t slot = (page_ == DxPage::FX1) ? 0 : 1;
            uint8_t offset = (uint8_t)(offsetof(RDX_Common, effects) + slot * 3 + 1);
            uint8_t newVal = clampAdd(p.common.effects[slot][1], delta, 0, 127);
            ipc_send_dx_raw_write(1, offset, newVal);
            break;
        }
        case DxPage::COUNT:
            break;  // unreachable
    }
}

const char* DX_Controller::pageName() const {
    switch (page_) {
        case DxPage::OP1:  return "OP1";
        case DxPage::OP2:  return "OP2";
        case DxPage::OP3:  return "OP3";
        case DxPage::OP4:  return "OP4";
        case DxPage::LFO:  return "LFO";
        case DxPage::ALGO: return "ALGO";
        case DxPage::FX1:  return "FX1";
        case DxPage::FX2:  return "FX2";
        case DxPage::COUNT: return "??";  // unreachable
    }
    return "??";
}
