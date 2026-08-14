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
// Two D5_HOT(next) in different classes land in the same named section and
// the linker rejects mixing a template's comdat with a plain function there.
// Functions that share a name across classes take a unique tag instead.
#  define D5_HOT_TAG(tag, f) __attribute__((section(".time_critical." #tag))) f
#else
#  define D5_HOT(f) f
#  define D5_HOT_TAG(tag, f) f
#endif
