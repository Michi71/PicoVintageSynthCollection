# UI host renderer

Renders the shared front-panel kit (`core/src/ui/kit.cpp`) on the Mac, with no
hardware and no pico-sdk, and writes each screen as a PBM.

```bash
./build_ui_kit.sh          # builds the u8g2 host archive on first run
./ui_kit_shots out         # writes out/*.pbm
./to_png.sh out            # out/sheet.png, 3x, in the colour of the OLED
```

The two sheets at the top of the repository README come from here:

```bash
cd tools/host_tests/md && NOLABEL=1 ../ui/to_png.sh out 3 && cp out/sheet.png ../../../img/ui_md.png
cd tools/host_tests/ui && NOLABEL=1 \
  ONLY="j6_vcf md_osc1 half_empty panel_name panel_name_hold panel_name_scroll diagnostics about" \
  ./to_png.sh out 3 && cp out/sheet.png ../../../img/ui_kit.png
```

`ONLY` keeps the kit's own checks (the empty body box, the two ends of a
knob, the popup and the two-column browser no instrument uses yet) out of a
sheet meant for players.

`../md/build_md_ui.sh` does the same for the *real* PicoFaceMD panel: it links
the unmodified `MD_Controller` against the unmodified kit, walks it through its
sections with the same encoder calls the panel makes, and renders what it
reports. Those screens are the panel, not an impression of it.

## Why this is trustworthy

Everything above the byte transport is the code that runs on the RP2350 - the
same u8g2, the same fonts, the same glyph metrics, the same kit. Only the
transport is replaced, by nothing at all: `u8x8_byte_empty`.

The one place where a host and a target usually drift is floating point, so the
kit draws its knob pointer from a table instead of from `sinf`/`cosf`. That is
what makes these files exact rather than approximate, and what would let a
stored set serve as a pixel-level regression test.

## Why it is worth having

A layout question used to cost a firmware build, a flash cycle and a look at
the panel. It now costs about a second, which is the difference between trying
three variants and settling for the first.
