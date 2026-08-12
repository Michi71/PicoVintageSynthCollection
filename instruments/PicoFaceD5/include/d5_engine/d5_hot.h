// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// RAM placement for the audio-rate functions, same pattern as cp_hot.h and
// the RD engine: on the Pico they go to .time_critical, on the host the
// macro vanishes. Everything the sample loop touches belongs in RAM -- the
// XIP cache is shared with the sample blob, and a strided PCM read evicting
// the code that reads it is how this instrument spent a day at 167% load.
#pragma once

#if defined(__has_include)
#  if __has_include("pico.h")
#    include "pico.h"
#  endif
#endif

#ifdef __not_in_flash_func
#  define D5_HOT(f) __not_in_flash_func(f)
#else
#  define D5_HOT(f) f
#endif
