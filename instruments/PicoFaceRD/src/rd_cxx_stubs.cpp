// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// rd_cxx_stubs.cpp - PicoFaceRD only.
//
#include "pico/stdlib.h"

// libstdc++ exception stubs: the firmware builds with -fno-exceptions, but
// std::vector's failure paths still reference std::__throw_* from libstdc++,
// which drags the ARM unwinder (~2 KB RAM) and the C++ name demangler
// (~30 KB flash) into the link. A failed allocation or a length error on
// this system is fatal either way -- panic instead of unwinding.
namespace std {
__attribute__((noreturn)) void __throw_length_error(const char* what) { panic("std: %s", what); }
__attribute__((noreturn)) void __throw_logic_error(const char* what)  { panic("std: %s", what); }
__attribute__((noreturn)) void __throw_bad_alloc()                    { panic("std: bad_alloc"); }
__attribute__((noreturn)) void __throw_bad_array_new_length()         { panic("std: bad_array_new_length"); }
}
