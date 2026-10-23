#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The host tour's screens on one sheet: tools/montage.py DIR OUT [COLUMNS]"""
import sys
from pathlib import Path
from PIL import Image

d, out = Path(sys.argv[1]), sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
ims = [Image.open(p) for p in sorted(d.glob("*.png"))]
w, h = ims[0].size
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 8) + 8, rows * (h + 8) + 8), (40, 40, 44))
for i, im in enumerate(ims):
    sheet.paste(im, (8 + (i % cols) * (w + 8), 8 + (i // cols) * (h + 8)))
sheet.save(out)
print(out, sheet.size)
