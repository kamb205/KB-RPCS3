# KB-RPCS3 visual identity

Original artwork for KB-RPCS3. None of it is derived from Sony or PlayStation
branding, and none of it is upstream RPCS3 artwork.

The mark is an original stylised numeral **3**: two right-facing bowls joined by a
short middle stroke, drawn from primitives in `kb-rpcs3-mark.svg` and
`port/make-kb-icon.py` (the console title icon).

## Files

| File | Use | Size |
|---|---|---|
| `kb-rpcs3-mark.svg` / `.png` | the numeral-3 mark on its own, transparent | 1024×1024 |
| `kb-rpcs3-app-icon.svg` / `.png` | square app/tile icon with wordmark and version | 1024×1024 |
| `kb-rpcs3-banner.svg` / `.png` | wide README banner | 1600×500 |
| `kb-rpcs3-social-preview.svg` / `.png` | GitHub social-preview image (2:1) | 1280×640 |
| `kb-rpcs3-release.svg` / `.png` | release artwork | 1280×640 |

The `.svg` files are the editable source of truth. Regenerate every PNG with:

```sh
python3 -m venv .venv
.venv/bin/pip install cairosvg Pillow
.venv/bin/python assets/branding/render.py
```

## Palette

| Role | Hex |
|---|---|
| Ink (background gradient) | `#0B111C` → `#18284C` |
| Cyan (mark, accents) | `#00D6E8` / `#00A8CF` |
| Border cyan | `#00BED6` |
| Foreground | `#E8F6FF` |
| Muted text | `#96CDDC` / `#9DBECE` / `#6E93A3` |

## Type

Bold sans for the wordmark and headings, regular sans for support text. The SVGs
name `Arial, Helvetica, sans-serif` so they render consistently anywhere a
Helvetica-metric font is available.

## Usage

- Keep the mark cyan-on-dark or cyan-on-transparent; do not place it on a light
  background without a dark plate.
- Do not add a Sony, PlayStation or RPCS3 logo to the artwork, and do not imply
  endorsement by either.
- Keep the wordmark "KB-RPCS3" as one word with hyphens; the product name is never
  spelled "KB RPCS3" or "KBRPCS3".
