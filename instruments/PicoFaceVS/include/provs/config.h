// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// provs/config.h - what is available at build time
#pragma once

// The Behringer parts (patches of the Pro VS Mini, chord mode) are optional.
// gen/pvm_state.h sets PROVS_HAS_PVM: tools/gen_data.py creates the file with
// 0, tools/gen_pvm.py switches it to 1 as soon as it has generated data.
#include "provs/gen/pvm_state.h"

// PROVS_HOT marks the per-sample path. On the Pico (RP2040/RP2350) it is
// placed in RAM: code running from XIP flash that does not fit into the
// cache is considerably slower. On a desktop host the marker has no effect.
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define PROVS_HOT __attribute__((noinline, section(".time_critical.provs")))
#else
#define PROVS_HOT
#endif
