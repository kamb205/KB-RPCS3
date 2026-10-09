#!/usr/bin/env python3
# KB-RPCS3 Alpha icon: an ORIGINAL stylised numeral 3, drawn from primitives.
# Not derived from Sony's PlayStation 3 logo and not the upstream RPCS3 logo.
# Run in the Lima VM (has Pillow); writes app/ps5/sce_sys/icon0.png (512x512).
import sys
from PIL import Image, ImageDraw, ImageFont

S = 2048  # supersample, downscale for smooth edges
img = Image.new("RGB", (S, S))
d = ImageDraw.Draw(img)

# 1. Vertical gradient background (dark teal -> navy).
top, bot = (14, 22, 30), (24, 40, 76)
for y in range(S):
    t = y / (S - 1)
    d.line([(0, y), (S, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)))

# 2. Rounded border.
m = int(S * 0.055)
d.rounded_rectangle([m, m, S - m, S - m], radius=int(S * 0.17),
                    outline=(0, 190, 214), width=int(S * 0.018))

# 3. The stylised "3": two right-facing semicircular bowls joined at the middle, plus a short middle stub.
cyan, white = (0, 214, 232), (232, 246, 255)
rw, rh = int(S * 0.15), int(S * 0.12)   # bowl half-width / half-height
w = int(S * 0.055)                      # stroke width
stub = int(rw * 0.45)                   # how far the middle stroke reaches left of the bowls' axis
# Centre the glyph: it spans [axis - stub, axis + rw + w/2] horizontally, so shift the axis left.
cx = (S - (rw + w // 2 - stub)) // 2
top_y = int(S * 0.14)                   # keep clear of the border (top) and the wordmark (bottom)
cy = top_y + rh                         # centre of the top bowl; the glyph ends at cy + 3*rh (~0.62 S)
d.arc([cx - rw, cy - rh, cx + rw, cy + rh], start=-90, end=90, fill=cyan, width=w)          # top bowl
d.arc([cx - rw, cy + rh, cx + rw, cy + 3 * rh], start=-90, end=90, fill=cyan, width=w)       # bottom bowl
d.line([(cx - stub, cy + rh), (cx + rw, cy + rh)], fill=cyan, width=w)                       # middle stroke

# 4. Wordmark.
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", int(S * 0.105))
word = "KB-RPCS3"
tw = d.textlength(word, font=font)
d.text(((S - tw) / 2, int(S * 0.72)), word, font=font, fill=white)
sub = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", int(S * 0.055))
sw = d.textlength("Alpha 0.1.0", font=sub)
d.text(((S - sw) / 2, int(S * 0.85)), "Alpha 0.1.0", font=sub, fill=(150, 205, 220))

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/kb-icon.png"
img.resize((512, 512), Image.LANCZOS).save(out)
print("wrote", out)
