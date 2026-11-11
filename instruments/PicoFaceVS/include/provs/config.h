// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// provs/config.h - was beim Bauen zur Verfuegung steht
#pragma once

// Die Behringer-Teile (Patches des Pro VS Mini, Chord-Modus) sind optional.
// gen/pvm_state.h setzt PROVS_HAS_PVM: tools/gen_data.py legt die Datei mit 0
// an, tools/gen_pvm.py stellt sie auf 1, sobald es Daten erzeugt hat.
#include "provs/gen/pvm_state.h"

// PROVS_HOT markiert den Abtastpfad. Auf dem Pico (RP2040/RP2350) kommt er
// ins RAM: aus dem XIP-Flash laeuft Code, der nicht in den Cache passt,
// deutlich langsamer. Auf dem Rechner hat die Markierung keine Wirkung.
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define PROVS_HOT __attribute__((noinline, section(".time_critical.provs")))
#else
#define PROVS_HOT
#endif
