#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
#
# to_png.sh <dir> [columns] -- turns the PBMs of a shot run into one contact
# sheet at 3x, in the pale blue of the actual OLED, so a layout can be judged
# at the size it will be read.
set -e
DIR="${1:-out}"
COLS="${2:-3}"
python3 - "$DIR" "$COLS" <<'PY'
import sys, glob, os
from PIL import Image, ImageDraw

d, cols = sys.argv[1], int(sys.argv[2])

def load(path):
    t = open(path).read().split()
    w, h = int(t[1]), int(t[2])
    px = t[3:]
    im = Image.new('RGB', (w, h), (0, 0, 0))
    p = im.load()
    for y in range(h):
        for x in range(w):
            if px[y * w + x] == '1':
                p[x, y] = (208, 232, 255)
    return im

files = sorted(glob.glob(os.path.join(d, '*.pbm')))
if not files:
    raise SystemExit('no .pbm in ' + d)

S, pad, cap = 3, 10, 14
cw, ch = 128 * S + pad * 2, 64 * S + pad + cap
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cw * cols, ch * rows), (24, 24, 28))
dr = ImageDraw.Draw(sheet)
for i, f in enumerate(files):
    im = load(f).resize((128 * S, 64 * S), Image.NEAREST)
    x, y = (i % cols) * cw + pad, (i // cols) * ch + pad
    dr.rectangle([x - 2, y - 2, x + 128 * S + 1, y + 64 * S + 1], outline=(95, 95, 105))
    sheet.paste(im, (x, y))
    dr.text((x, y + 64 * S + 3), os.path.basename(f)[:-4], fill=(160, 160, 170))

out = os.path.join(d, 'sheet.png')
sheet.save(out)
print(out, sheet.size)
PY
