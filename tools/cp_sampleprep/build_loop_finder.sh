#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# build_loop_finder.sh -- build the one C++ tool the Python pipeline shells out to.
#
# prepare_samples.py calls this binary once per candidate window; its default
# path is tools/cp_sampleprep/FindLoopPoints, which is where this writes it.
# Override with the loop_finder.binary field in a config.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-c++}"

"$CXX" -O2 -std=c++17 -o "$HERE/FindLoopPoints" "$HERE/src/FindLoopPoints.cpp" -lm

echo "[ok]   $HERE/FindLoopPoints" >&2
