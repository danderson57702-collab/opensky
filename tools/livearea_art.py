#!/usr/bin/env python3
"""OpenSKY's own LiveArea pictures for the VPK (engine/sce_sys/livearea/contents): a plain dark background
and the start gate with OpenSKY's icon. No game picture is in them (the VPK may carry none); a player's
own view replaces the background from the game's Options ("Use this view for the LiveArea", livearea.c).
The LiveArea takes 8-bit palette PNGs: bg.png 840x500, startup.png 280x158."""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "engine", "sce_sys", "livearea", "contents")
ICON = os.path.join(HERE, "..", "engine", "sce_sys", "icon0.png")
FONT = "/System/Library/Fonts/Avenir Next.ttc"


def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        for x in range(w):
            px[x, y] = c
    return img


def font(size):
    try:
        return ImageFont.truetype(FONT, size, index=5)  # (Avenir Next Demi Bold)
    except OSError:
        return ImageFont.load_default()


def save_indexed(img, name):
    img.convert("P", palette=Image.ADAPTIVE, colors=256).save(os.path.join(OUT, name), optimize=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    icon = Image.open(ICON).convert("RGBA")

    bg = gradient(840, 500, (36, 44, 54), (12, 15, 19))
    save_indexed(bg, "bg.png")

    gate = gradient(280, 158, (44, 53, 64), (20, 24, 30))
    small = icon.resize((96, 96), Image.LANCZOS)
    gate.paste(small, (20, 31), small)
    d = ImageDraw.Draw(gate)
    d.text((128, 58), "OpenSKY", font=font(30), fill=(232, 236, 240))
    d.text((130, 96), "Start", font=font(16), fill=(160, 172, 186))
    save_indexed(gate, "startup.png")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
