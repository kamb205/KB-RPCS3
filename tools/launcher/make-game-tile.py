#!/usr/bin/env python3
"""Make a PS5 home-screen tile (a ShadowMountPlus folder title) for one PS3 game in RPCS3 PS5.

The tile's eboot is the launcher (tools/launcher/main.c): it starts RPCS3 PS5 with "--boot <path>".

  make-game-tile.py --serial BLES01439 --name "WWE '12" --boot /app0/rpcs3/games/WWE12 \
      --icon0 ICON0.PNG --pic1 PIC1.PNG [--cover cover.jpg] --launcher ~/work/tile-launcher/app --out ~/work/tiles

Art: icon0.png 512x512 (the tile) and pic0.png 3840x2160 (the background), as real PS5 games have.
With --cover (box art), the tile shows the cover; otherwise the game's ICON0 on its own PIC1.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import json
import os
import shutil
import zlib

from PIL import Image, ImageFilter, ImageEnhance


def tile_title_id(serial):
    # PPSA8xxxx, stable per PS3 serial (PS5 title IDs are 4 letters + 5 digits)
    return "PPSA8%04d" % (zlib.crc32(serial.encode()) % 10000)


def cover_crop(img, w, h):
    """Scale and centre-crop img to fill w x h."""
    scale = max(w / img.width, h / img.height)
    img = img.resize((max(w, round(img.width * scale)), max(h, round(img.height * scale))), Image.LANCZOS)
    left, top = (img.width - w) // 2, (img.height - h) // 2
    return img.crop((left, top, left + w, top + h))


def fit(img, w, h):
    scale = min(w / img.width, h / img.height)
    return img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)


def backdrop(img, size):
    bg = cover_crop(img, size, size).filter(ImageFilter.GaussianBlur(size // 24))
    return ImageEnhance.Brightness(bg).enhance(0.55)


def make_icon(icon0, pic1, cover):
    size = 512
    if cover is not None:
        art = cover.convert("RGB")
        canvas = backdrop(art, size)
        front = fit(art, size, size)
    else:
        art = icon0.convert("RGBA")
        canvas = backdrop((pic1 or icon0).convert("RGB"), size)
        front = fit(art, size - 24, size - 24)
    canvas = canvas.convert("RGBA")
    canvas.alpha_composite(front.convert("RGBA"), ((size - front.width) // 2, (size - front.height) // 2))
    return canvas.convert("RGB")


def make_pic0(icon0, pic1, cover):
    w, h = 3840, 2160
    if pic1 is not None:
        return cover_crop(pic1.convert("RGB"), w, h)
    src = (cover or icon0).convert("RGB")
    return ImageEnhance.Brightness(cover_crop(src, w, h).filter(ImageFilter.GaussianBlur(40))).enhance(0.6)


def param_json(title_id, name):
    return {
        "ageLevel": {"default": 0},
        "applicationCategoryType": 0,
        "applicationDrmType": "free",
        "attribute": 0,
        "attribute2": 0,
        "attribute3": 524352,
        "conceptId": title_id[4:],
        "contentBadgeType": 1,
        "contentId": "UP9000-%s_00-RPCS3TILE0000000" % title_id,
        "contentVersion": "01.000.000",
        "downloadDataSize": 0,
        "gameIntent": {"permittedIntents": [{"intentType": "launchActivity"}]},
        "localizedParameters": {"defaultLanguage": "en-US", "en-US": {"titleName": name}},
        "masterVersion": "01.00",
        "pubtools": {"creationDate": "2026-10-07 00:00:00", "loudnessSnd0": "-28.00", "toolVersion": "2.00"},
        "requiredSystemSoftwareVersion": "0x0000000000000000",
        "sdkVersion": "0x0000000000000000",
        "titleId": title_id,
        "versionFileUri": "",
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--boot", required=True, help="path RPCS3 PS5 boots, as its library lists it")
    ap.add_argument("--icon0", required=True)
    ap.add_argument("--pic1")
    ap.add_argument("--cover")
    ap.add_argument("--launcher", required=True, help="folder with eboot.bin and sce_module/")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    title_id = tile_title_id(a.serial)
    content_id = "UP9000-%s_00-RPCS3TILE0000000" % title_id
    assert len(content_id) == 36, content_id
    out = os.path.join(a.out, title_id)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "sce_sys"))
    shutil.copy(os.path.join(a.launcher, "eboot.bin"), out)
    shutil.copytree(os.path.join(a.launcher, "sce_module"), os.path.join(out, "sce_module"))

    icon0 = Image.open(a.icon0)
    pic1 = Image.open(a.pic1) if a.pic1 else None
    cover = Image.open(a.cover) if a.cover else None
    make_icon(icon0, pic1, cover).save(os.path.join(out, "sce_sys", "icon0.png"), optimize=True)
    make_pic0(icon0, pic1, cover).save(os.path.join(out, "sce_sys", "pic0.png"), optimize=True)
    with open(os.path.join(out, "sce_sys", "param.json"), "w") as f:
        json.dump(param_json(title_id, a.name), f, indent=2)
    with open(os.path.join(out, "rpcs3-launch.txt"), "w") as f:
        f.write(a.boot + "\n")
    print("%s: %s (%s) -> %s" % (title_id, a.name, a.serial, out))


if __name__ == "__main__":
    main()
