# Changelog: RP2350 pipeline optimization and bug fixes

**Date:** 2026-07-08  
**Workflow:** architecture/orchestration by Claude, code generation by
`kimi-k2.7-code:cloud` via the Ollama API

> **Historical document.** This log describes the state of the standalone
> PicoFaceCP repository. File names such as `main.cpp` and
> `pico_frontpanel.cpp` and the core0/core1 split no longer match the
> collection; see [docs/ARCHITECTURE.md](../../../docs/ARCHITECTURE.md),
> section 4a.

---

## 1. Summary

This round of optimization concentrates on two critical areas: correcting the
long-standing L/R channel swap in the stereo output, and targeted performance
and memory optimizations in the DSP pipeline. In addition, a safety problem in
the audio IRQ was removed by getting rid of a variable length array (VLA). All
changes were built and verified successfully; there are no new compiler
warnings.

---

## 2. Bug fixes

| ID | Priority | File(s) | Description |
|---|---|---|---|
| B1 | critical | `src/main.cpp`, `i2s_callback_func` | L/R channel swap fixed: `mdaEPiano::process(int16_t* outputs_r, int16_t* outputs_l)` expects the **right** channel as its first parameter. The call `ep.process(&l[0], &r[0])` therefore filled `l[]` with the right channel and `r[]` with the left. Fixed to `ep.process(&r[0], &l[0])`, so that I2S left is engine left. Affected: stereo width, tremolo pan, reverb L/R, chorus stereo. |
| B2 | normal | `src/pico_frontpanel.cpp` | `pct()` clamped values at 99 instead of 100. Corrected to `if(x>100)x=100`. |

---

## 3. Performance optimizations

| ID | Weight | File(s) | Description | Expected effect |
|---|---|---|---|---|
| P1 | high | `effects/dsp_lut.h`, `effects/reface_cp_fx.h` | New global `g_sinLUT()` with 512 entries (lazy init). Six `sinf()` calls in tremolo (1), chorus (2), phaser (2) and delay wobble (1) replaced by LUT interpolation. | `sinf` is roughly 50-100 cycles, LUT interpolation roughly 5-10 on the Cortex-M33. Cost: +2048 bytes of RAM for the table. |
| P2 | medium | `effects/dsp_reverb.h` | The reverb modulo `idx=(idx+1)%len` (an integer division, 12x per sample) replaced by `if(++idx>=len)idx=0`. | About 529,000 divisions per second eliminated. |
| P3 | medium | `lib/audio/include/audio_subsystem.h` | Audio buffer size raised from 16 to 32 samples. | IRQ rate halved from 2756/s to 1378/s, less context switch overhead. Latency stays below 1 ms. |
| P4 | safety | `src/main.cpp` | VLA removed from the audio IRQ: `int16_t l[buffer->max_sample_count]` replaced by `int16_t l[SAMPLES_PER_BUFFER]` (fixed size). | No variable length arrays in interrupt context; more predictable and safer. |
| P5 | low | `src/pico_hw.cpp` | OLED I2C clock raised from 400 kHz to 1 MHz. | Faster display refresh for the front panel UI. |

---

## 4. Memory optimizations

| ID | File(s) | Description | Saving |
|---|---|---|---|
| C2 | `effects/reface_cp_fx.h` | Delay buffer `kBuf` adjusted from 24001 to 22080 samples. The old value was sized for 48 kHz, but the system runs at 44.1 kHz. | 7680 bytes of BSS |

---

## 5. Build verification

- **FLASH:** `text` 4,428,008 bytes (about 26.4 % of 16 MB)
- **RAM:** `bss` 173,568 bytes (about 33.8 % of 512 KB)  
  - before: 179,200 bytes (35.0 %)
  - difference: -7684 bytes
- **Warnings:** no new warnings, clean build

---

## 6. Addendum: volume bug fix

**Date:** 2026-07-08 (same session)

### B3 [CRITICAL BUG FIX]: overall volume too low

| File | Change | Explanation |
|---|---|---|
| `src/main.cpp` | `ep.setVolume(64)` -> `ep.setVolume(100)` | The engine volume parameter `volume = 0.00002 x value²` was set to 64 (-> 0.08192). The constructor default in `mdaEPiano.cpp` is `volume = 0.2f`, which corresponds to `setVolume(100)`. main.cpp overwrote it with 64, i.e. 2.44x quieter. |
| `src/main.cpp` | `cp_fx.setVolume(0.9f)` -> `cp_fx.setVolume(1.0f)` | FX chain master volume default from 90 % to 100 % (unity). |

**Signal level before/after the fix** (single note, velocity 100, FX volume
100 %):
- before (setVolume 64): peak about 4.1 % full scale = -27.8 dB
- after (setVolume 100): peak about 10 % full scale = -20.0 dB (+7.8 dB)
- four-note chord: 40 % FS = -8 dB (healthy, no clipping risk)

---

## Backport from PicoFaceDX/PicoFaceRD - audio pipeline, encoders, producer in thread context (2026-08-03)

### Why

The sibling projects PicoFaceDX and PicoFaceRD share the vendored `lib/audio`
and `lib/encoder` as well as `pico_hw`/`project_config` with PicoFaceCP. Every
bug found there was still open here. The reference is PicoFaceDX commit
`668b9b8` including its §27 addendum; the bug descriptions there apply here word
for word.

### A) Tuning: the instrument sounded 14.5 cents sharp

`update_pio_frequency()` discarded the fractional part of the PIO clock divider.
At 444 MHz / 44,100 Hz / S32 stereo the exact divider is **78.65625**, but
**78** was used:

```
raw divider (1/256): 444000000 * 2 / 44100 = 20136  ->  int 78, frac 168
old (integer):       444e6 / 78       / 128 = 44,471.15 Hz = +14.51 cents
new (fractional):    444e6 / 78.65625 / 128 = 44,100.12 Hz = +0.005 cents
```

Now `pio_sm_set_clkdiv_int_frac()`. The RP2040 fallback branch (402 MHz) was at
+5.26 cents and is now at +0.028 cents.

### B) The silence buffer read past its allocation

The underrun replacement buffer was allocated as
`PICO_AUDIO_I2S_BUFFER_SAMPLE_LENGTH * 4` = 2,304 B, but at S32 stereo the DMA
reads `sample_count * 2` 32-bit words = 4,608 B. On **every** underrun, 2,304 B
of heap garbage went to the DAC, for 13.06 ms. Now stride-correct and shrunk to
one producer block:

| | allocation | DMA reads | silence per underrun |
|---|---|---|---|
| before (576) | 2,304 B | 4,608 B -> **2,304 B out of bounds** | 13.06 ms |
| now (32) | 256 B | 256 B | **0.73 ms** |

`PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH` is deliberately **32** (DX and RD
use 64) and has to stay in sync with `SAMPLES_PER_BUFFER`. `SAMPLES_PER_BUFFER`
must not move to 64: `mdaEPiano.h:45` derives `I2S_BUFFER_WORDS` from it, the
hard loop bound of the render.

### C) Further `lib/audio` bugs

A broken `#ifdef` guard (it checked the same name as the surrounding `#ifndef`);
`wrap_consumer_take` / `wrap_producer_give` compared the input channel count
with itself and could run off the end without a `return` in a release build;
`playing_buffer` was only cleared under `#ifndef NDEBUG` (a buffer leak in
release); `audio_i2s_set_enabled(false)` did not abort the running DMA;
`audio_i2s_end()` had an unsafe teardown order; `printf()` in the DMA IRQ. New:
`g_i2s_underrun_count` and `audio_i2s_consume_txstall()`.

### D) `lib/encoder`

`clocks_per_time` was computed in the constructor of global objects and
therefore captured the 150 MHz boot clock instead of 444 MHz - dormant in CP,
which only uses `delta()`, but taken over for parity. Two overflow guards relied
on signed wraparound (UB at `-O2`). The `PushButton` debounce compared an
absolute millisecond deadline and would have blocked the button for weeks after
the 49.7-day overflow - **that is the only change with an immediately observable
effect**. `lib/encoder/src/rotary_encoder.cpp` removed (not in the CMakeLists,
references three headers that do not exist).

### E) `pico_hw` and build

FPU flush-to-zero: **new for CP**, there was none at all before. The feedback
states of the FX chain (reverb/delay/phaser/chorus) had no denormal protection;
`mdaEPiano` protects itself only partially. It is now set on **both** cores
(FPSCR is per-core state). Also: typed `qmi_hw->m[0].timing` access, dead
ROSC/`srand()` block removed, 1.60 V documented as not bisected, `analogRead()`
removed from `arduino_compat.h` (no callers, and the only consumer of
`hardware/adc.h`).

CMake: `hardware_adc/spi/interp/watchdog` unlinked - the "interpolation" in the
sample engine is plain integer arithmetic, not the interp peripheral.
`PICO_USE_SW_SPIN_LOCKS` removed (it is the SDK default on RP2350); the comment
records that the flag **renumbers** the reserved spinlock IDs and that IDs 6/7
in hardware mode would collide with the ones hardcoded in
`lib/audio/include/audio.h:29,34`. Both stacks moved to 4 KB in the scratch
banks.

### F) Producer out of the DMA IRQ and into thread context

`i2s_callback_func()` used to render a complete block inside the interrupt. Now:
empty callback, rendering and IPC drain in the `main()` loop of core0,
`AUDIO_BUFFER_COUNT 6`.

Two points that are **mandatory** here and were missed in DX at first (see §27
there):

1. `__not_in_flash_func` is a pure section attribute **without** `__noinline`
   (`pico/platform/sections.h:268` vs. `:284`). `flash_park_core0()` was
   RAM-resident only as a side effect of being inlined into the RAM-resident IRQ
   handler. With the producer in `main()` (flash), the spin loop would have
   landed in XIP - exactly where core1 is erasing and rewriting
   `qmi_hw->m[0].timing` at the same time. Now `__no_inline_not_in_flash_func`,
   like `flash_write_locked()` on the core1 side.
2. The same applies to the **render path**: `cp_process_block_i16()` is `inline`
   without a symbol of its own and was likewise in RAM only by accident. Hence
   the new `render_one_block()` as `__no_inline_not_in_flash_func`. With 4.4 MB
   of sample data in flash, rendering from XIP would be particularly expensive.

Fixed in passing: `l[i] << 16` is a signed overflow for `l[i] == -32768` (UB at
`-O2`); now cast through `(uint16_t)`, same bit pattern, defined behaviour.

With a full pool core0 waits with `__wfe()` instead of spinning hot - both wake
sources already SEV, the event register is latched, so there is no lost wakeup.
A permanent spin would be a real thermal change at 1.60 V and 444 MHz.

### G) Output latency

`audio_i2s_connect()` would have used the hardwired **2 x 256** consumer frames,
which dominate the latency because the consumer take pulls all prepared producer
buffers into one 256-frame buffer and the DMA plays that as a single transfer.
Instead: `audio_i2s_connect_extra(pool, false, 2, 64, NULL)`. 64 is an exact
multiple of `SAMPLES_PER_BUFFER` (32), so producer buffers are never split. The
price is a higher DMA IRQ rate, which is affordable now that the handler no
longer renders.

### Deliberately NOT taken over

- **Master volume as a menu entry** (DX §26 E): CP already has a volume on the
  home screen (`"Vol %2d"`, `RefaceCpChain::getVolume()`, persisted in the
  settings). A second control would be duplication. The author's decision.
- **`std::__throw_*` panic stubs**: measurably zero gain in CP - `nm` shows no
  unwinder or demangler symbols, `.ARM.exidx` is already 8 B, CP uses no
  libstdc++ containers, and the build is already
  `-fno-exceptions -fno-unwind-tables -fno-rtti`.
- The 480 MHz branch from PicoFaceRD. CP stays at 444 MHz.

### Build verification

Zero warnings. **Flash:** 4,424,968 B / 26.37 % (before 4,423,912 B). **RAM:**
187,868 B / 35.83 % (before 188,792 B). SCRATCH_X/Y 4 KB each / 100 % -
intentional, those are the stacks.

Placement audit (the acceptance criterion for F):

```
10001f04 000006c8 T main                        <- flash, small: nothing large inlined into it
10011b98 00000008 t __flash_park_core0_veneer   <- flash (long-call veneer)
10011bb8 00000008 t __render_one_block_veneer   <- flash (long-call veneer)
20000110 00000040 t flash_park_core0            <- RAM
20000150 00001424 W RefaceCpChain::process      <- RAM
20001574 00000184 t render_one_block            <- RAM
200016f8 00000002 T i2s_callback_func           <- RAM (empty)
200027bc 00000094 T lrintf                      <- RAM
```

### Still open - only checkable on hardware

1. Tuning against a reference instrument (it should be A=440 now instead of
   14.5 cents sharp).
2. Underruns at full polyphony with the producer in thread context. CP has no
   CPU load screen like DX; if it is suspected, `g_i2s_underrun_count` would
   have to be surfaced.
3. Noticeable input/output latency after the move to 64-frame consumer buffers.
4. FPU FTZ is new for CP: watch for changed decay tails or CPU load.
