// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from giulioz/rdpiano and MAME; copyright is shared with their authors.
// See instruments/PicoFaceRD/README.md.

#ifndef RD_PACKS_DATA_H
#define RD_PACKS_DATA_H

#include <stdint.h>
#include <stddef.h>

// How many patches this build ships, and which one it starts at. A build for
// a single machine carries half the packs and half the sample banks, which is
// what fits it on a 4 MB Pico 2; see instruments/PicoFaceRD/instrument.cmake.
// The firmware counts 0..RD_PATCH_COUNT-1 either way -- RD_PATCH_BASE is what
// turns that into the patch number the machine itself uses.
#ifndef RD_PATCH_COUNT
#define RD_PATCH_COUNT 16
#endif
#ifndef RD_PATCH_BASE
#define RD_PATCH_BASE 0
#endif

extern const uint8_t* const rd_pack_ptrs[RD_PATCH_COUNT];
extern const size_t rd_pack_sizes[RD_PATCH_COUNT];

#endif // RD_PACKS_DATA_H
