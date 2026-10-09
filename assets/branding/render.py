#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render the KB-RPCS3 branding SVGs to PNG.

The SVG files in this folder are the editable source of truth. This script only
rasterises them, so a change to a source file plus a re-run reproduces every PNG
deterministically.

    python3 -m venv .venv && .venv/bin/pip install cairosvg Pillow
    .venv/bin/python assets/branding/render.py

Requires cairosvg (and, for PNG optimisation, Pillow).
"""
from __future__ import annotations

import sys
from pathlib import Path

try:
    import cairosvg
except ImportError:  # pragma: no cover
    sys.exit("cairosvg is required: pip install cairosvg")

HERE = Path(__file__).resolve().parent

# source SVG -> (output PNG, width, height)
TARGETS = {
    "kb-rpcs3-mark.svg": ("kb-rpcs3-mark.png", 1024, 1024),
    "kb-rpcs3-app-icon.svg": ("kb-rpcs3-app-icon.png", 1024, 1024),
    "kb-rpcs3-banner.svg": ("kb-rpcs3-banner.png", 1600, 500),
    "kb-rpcs3-social-preview.svg": ("kb-rpcs3-social-preview.png", 1280, 640),
    "kb-rpcs3-release.svg": ("kb-rpcs3-release.png", 1280, 640),
}


def optimise(path: Path) -> None:
    """Re-save as an optimised, flattened PNG (branding art has no transparency)."""
    try:
        from PIL import Image
    except ImportError:
        return
    with Image.open(path) as im:
        im.convert("RGB").save(path, format="PNG", optimize=True)


def main() -> int:
    for svg_name, (png_name, width, height) in TARGETS.items():
        src = HERE / svg_name
        if not src.exists():
            sys.exit(f"missing source: {src}")
        dst = HERE / png_name
        cairosvg.svg2png(
            url=str(src), write_to=str(dst), output_width=width, output_height=height
        )
        optimise(dst)
        print(f"{svg_name} -> {png_name} ({width}x{height}, {dst.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
