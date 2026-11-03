#!/usr/bin/env python3
"""Generates assets/cna.ico (the app icon) from code: accent rounded square + white sparkle.
Run:  python3 tools/make_icon.py        (needs Pillow)  - the .ico is committed, so this is only
needed if you want to change the artwork."""
import os
from PIL import Image, ImageDraw

ACCENT = (10, 132, 232, 255)   # #0A84E8
SPARK = [(0.50, 0.16), (0.58, 0.42), (0.84, 0.50), (0.58, 0.58),
         (0.50, 0.84), (0.42, 0.58), (0.16, 0.50), (0.42, 0.42)]

def render(px):
    ss = 8                                  # supersample for smooth edges
    n = px * ss
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = max(1, int(n * 0.03))
    d.rounded_rectangle([pad, pad, n - pad - 1, n - pad - 1], radius=int(n * 0.22), fill=ACCENT)
    d.polygon([(x * n, y * n) for x, y in SPARK], fill=(255, 255, 255, 255))
    return img.resize((px, px), Image.LANCZOS)

sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
base = render(256)
out = os.path.join(os.path.dirname(__file__), "..", "assets", "cna.ico")
os.makedirs(os.path.dirname(out), exist_ok=True)
base.save(out, format="ICO", sizes=[(s, s) for s in sizes])
render(256).save(os.path.join(os.path.dirname(out), "cna-256.png"))
print("wrote", os.path.normpath(out), "sizes", sizes)

# ---- installer side artwork (164x314, shown on the welcome/finish pages) --------------------------
from PIL import ImageFont
W, H = 164, 314
side = Image.new("RGB", (W, H))
px = side.load()
top, bot = (10, 132, 232), (7, 52, 128)
for y in range(H):
    t = y / (H - 1)
    for x in range(W):
        px[x, y] = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
big = render(256).resize((112, 112), Image.LANCZOS)
white = Image.new("RGBA", (112, 112), (255, 255, 255, 255))
# draw just the sparkle (white) on the gradient, no square
d = ImageDraw.Draw(side)
cx, cy, s = W // 2, 92, 104
d.polygon([(cx - s / 2 + x * s, cy - s / 2 + y * s) for x, y in SPARK], fill=(255, 255, 255))
try:
    f1 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 19)
    f2 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
except OSError:
    f1 = f2 = ImageFont.load_default()
d.text((W // 2, 226), "Clean-n-Adapt", font=f1, fill=(255, 255, 255), anchor="mm")
d.text((W // 2, 250), "light  \u2022  safe  \u2022  fast", font=f2, fill=(205, 228, 252), anchor="mm")
side.save(os.path.join(os.path.dirname(out), "installer-side.bmp"), format="BMP")
print("wrote installer-side.bmp")
