/*
  j6_patchstore.cpp -- the 56 user memories, in one flash sector

  The flash write path is the one from veeprom.cpp and for the same reasons:
  after flash_range_erase or flash_range_program the SDK's boot2 re-init leaves
  the QMI M0_TIMING at a value that is not stable at the 444 MHz overclock, so
  the write has to run from SRAM and put the timing back before the next
  instruction is fetched from flash. Getting that wrong does not produce a bad
  patch, it hangs the chip.
*/

#include "j6_patchstore.h"

#include <string.h>

/* The whole sector, mirrored in RAM. 4 kB against 490 kB free, and it means a
 * read never touches flash and a write has one place to assemble from. */
static uint8_t s_bank[4096];
static bool    s_ready = false;

static j6_patch_lock_fn   s_lock   = nullptr;
static j6_patch_unlock_fn s_unlock = nullptr;

void j6_patchstore_set_lock_hooks(j6_patch_lock_fn lock, j6_patch_unlock_fn unlock)
{
    s_lock   = lock;
    s_unlock = unlock;
}

static J6UserPatch* slotPtr(int slot)
{
    return (J6UserPatch*) (s_bank + (size_t) slot * sizeof(J6UserPatch));
}

/* ------------------------------------------------------------------------ */
#ifdef JUNO_HOST_BUILD

/*
 * On the host the bank lives in RAM and nothing is persisted. That is enough
 * for the self test to exercise writing, reading back and slot bookkeeping,
 * which is where the bugs are; the flash path itself can only be tested on the
 * device.
 */
static bool bank_load(void)  { memset(s_bank, 0xFF, sizeof(s_bank)); return true; }
static bool bank_store(void) { return true; }

#else

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "pico/platform.h"
#include "veeprom.h"
#include "project_config.h"

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096u
#endif

/*
 * Immediately below the two sectors the veeprom uses, so the two never meet.
 * A change here orphans every stored patch, which is why it is derived from the
 * veeprom's own layout rather than written out as a number.
 */
#define J6_PATCH_FLASH_OFFSET \
    (PICO_FLASH_SIZE_BYTES - (VEEPROM_NUM_SECTORS + 1u) * FLASH_SECTOR_SIZE)

static bool bank_load(void)
{
    memcpy(s_bank, (const uint8_t*) (XIP_BASE + J6_PATCH_FLASH_OFFSET),
           sizeof(s_bank));
    return true;
}

/* Erase and reprogram the sector from s_bank. RAM-resident: no instruction may
 * be fetched from flash between the erase and the QMI timing being restored. */
static void __no_inline_not_in_flash_func(bank_write_locked)(const uint8_t* buf)
{
    uint32_t ints = save_and_disable_interrupts();

    flash_range_erase(J6_PATCH_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(J6_PATCH_FLASH_OFFSET, buf, FLASH_SECTOR_SIZE);

#if PICO_RP2350
    qmi_hw->m[0].timing = PICOFACE_QMI_M0_TIMING_OC;   /* undo boot2 clobber */
    /* Returning from this SRAM-resident function is the first fetch to go
     * through the QMI again, so the write must have landed. __dsb orders the
     * APB write against those fetches; a compiler barrier would not. */
    __dsb();
    __isb();
#endif

    restore_interrupts(ints);
}

static bool bank_store(void)
{
    if (s_lock && !s_lock()) return false;
    bank_write_locked(s_bank);
    if (s_unlock) s_unlock();
    return true;
}

#endif /* JUNO_HOST_BUILD */

/* ------------------------------------------------------------------------ */
void j6_patchstore_init(void)
{
    bank_load();
    s_ready = true;
}

bool j6_patch_valid(int slot)
{
    if (!s_ready || slot < 0 || slot >= J6_USER_PATCHES) return false;
    return slotPtr(slot)->magic == J6_PATCH_MAGIC;
}

const char* j6_patch_name(int slot)
{
    if (!j6_patch_valid(slot)) return "";

    /* The stored name is NUL-padded but a corrupt record could fill all twelve
     * bytes, so the last one is forced to a terminator before it is handed out
     * as a C string. */
    J6UserPatch* p = slotPtr(slot);
    p->name[J6_PATCH_NAME_LEN - 1] = 0;
    return p->name;
}

bool j6_patch_read(int slot, J6UserPatch& dst)
{
    if (!j6_patch_valid(slot)) return false;
    memcpy(&dst, slotPtr(slot), sizeof(J6UserPatch));
    dst.name[J6_PATCH_NAME_LEN - 1] = 0;
    return true;
}

bool j6_patch_erase(int slot)
{
    if (!s_ready || slot < 0 || slot >= J6_USER_PATCHES) return false;
    if (!j6_patch_valid(slot)) return true;      /* already free */

    memset(slotPtr(slot), 0xFF, sizeof(J6UserPatch));
    return bank_store();
}

bool j6_patch_write(int slot, const J6UserPatch& src)
{
    if (!s_ready || slot < 0 || slot >= J6_USER_PATCHES) return false;

    memcpy(slotPtr(slot), &src, sizeof(J6UserPatch));
    slotPtr(slot)->magic = J6_PATCH_MAGIC;
    slotPtr(slot)->name[J6_PATCH_NAME_LEN - 1] = 0;

    return bank_store();
}
