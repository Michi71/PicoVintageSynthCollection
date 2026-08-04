/*
  j6_patchstore.h -- the 56 user memories

  A Juno-60 stores 56 patches of its own, eight per bank across seven banks,
  and it has a WRITE button and a PROTECT switch to go with them. This is that,
  in one 4 kB flash sector.

  Why not the veeprom: that is an append log which keeps the single most recent
  record, with 240 bytes of payload. It is exactly right for "the state the
  instrument was left in" and no use at all for 56 independent memories. So the
  patches get their own sector, immediately below the two the veeprom uses.

  One record is 72 bytes and 56 of them come to 4032, which is why the name is
  twelve characters and not sixteen -- at sixteen they would not fit in a sector
  and a write would have to erase two, doubling the time the audio is stopped.

  Writing costs an erase of the whole sector, because that is the smallest thing
  flash can erase. The sector is read into RAM, the one record changed, and the
  whole thing written back. That takes some tens of milliseconds with interrupts
  off, so the audio stops for an audible moment -- acceptable for something the
  player asked for by pressing a button, and the reason writing is not offered
  anywhere it could happen by accident.
*/

#ifndef J6_PATCHSTORE_H
#define J6_PATCHSTORE_H

#include <stdint.h>
#include <stddef.h>
#include "juno/juno_params.h"

#define J6_USER_PATCHES   56
#define J6_PATCH_NAME_LEN 12

/* Characters a name can actually hold -- the field less its terminator. Naming
 * is done one position at a time, so this is also how far the cursor goes. */
#define J6_NAME_EDIT_LEN  (J6_PATCH_NAME_LEN - 1)

/*
 * One memory. Only the patch half of the parameter list is stored: the
 * arpeggiator and the master volume are instrument settings and belong to the
 * veeprom record, not to a sound.
 */
struct __attribute__((packed)) J6UserPatch {
    uint16_t magic;                        /* J6_PATCH_MAGIC when in use   */
    char     name[J6_PATCH_NAME_LEN];      /* NUL-padded, may be truncated */
    uint16_t param[JUNO_PARAM_COUNT];      /* 0..1000 per mille each       */
};

#define J6_PATCH_MAGIC 0x364Au   /* "J6" */

static_assert(sizeof(J6UserPatch) == 2 + J6_PATCH_NAME_LEN + 2 * JUNO_PARAM_COUNT,
              "J6UserPatch has gained padding");
static_assert(sizeof(J6UserPatch) * J6_USER_PATCHES <= 4096,
              "the user patches no longer fit in one flash sector");

/*
 * Core-parking hooks, the same shape as the veeprom's and for the same reason:
 * a flash operation takes the XIP window away from both cores, so anything
 * running on the other one has to be parked in RAM first.
 *
 * Both are no-ops today because core 1 is idle. They exist so that moving the
 * voices onto core 1 -- which is the documented route to 2x oversampling --
 * does not turn a patch write into a hang that only shows up on the device.
 */
typedef bool (*j6_patch_lock_fn)(void);
typedef void (*j6_patch_unlock_fn)(void);

void j6_patchstore_set_lock_hooks(j6_patch_lock_fn lock, j6_patch_unlock_fn unlock);

/* Reads the sector. Call once at start-up, after veeprom_init. */
void j6_patchstore_init(void);

/* True if the slot holds a patch. */
bool j6_patch_valid(int slot);

/* The stored name, or an empty string for a free slot. Points into the store's
 * own copy and stays valid until the next write. */
const char* j6_patch_name(int slot);

/* Copies the slot out. Returns false for a free or out-of-range slot, leaving
 * dst untouched. */
bool j6_patch_read(int slot, J6UserPatch& dst);

/*
 * Writes one slot. Erases and reprograms the sector, so it stops the audio for
 * a moment; see the note at the top. Returns false if the slot is out of range
 * or the flash write failed.
 */
bool j6_patch_write(int slot, const J6UserPatch& src);

/*
 * Frees a slot.
 *
 * The instrument has no such thing -- all 56 of its memories always hold
 * something and you overwrite. A free state only exists here, so it has to be
 * reachable, or the display can say "-free-" about something that can never
 * become free again.
 *
 * Erasing an already free slot does nothing at all, and in particular does not
 * touch the flash: no point spending a sector erase and a break in the audio to
 * clear a marker that is already clear.
 */
bool j6_patch_erase(int slot);

#endif /* J6_PATCHSTORE_H */
