#!/usr/bin/env python3
"""Make the mod plants' sprites from the player's own game data.

The mod art is derived from the original images (recolored), so it is generated on each machine
instead of being committed. Pass the game data folder (with main.pak, or extracted files) and the
output folder; the game loads them from <executable>/mods or <app>/Contents/Resources/mods.
"""
import argparse
import io
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from importlib import import_module

pak = import_module("extract-pak")

# name in the output folder -> (original image inside the game data, recolor)
# recolor: (hue 0-255, saturation factor, brightness factor)
ICE = (142, 0.55, 1.35)
PEA = (68, 1.0, 1.08)
SPIKY = (191, 0.75, 1.0)
MOD_IMAGES = {
    "IceNut_body.png": ("reanim/Wallnut_body.png", ICE),
    "IceNut_cracked1.png": ("reanim/Wallnut_cracked1.png", ICE),
    "IceNut_cracked2.png": ("reanim/Wallnut_cracked2.png", ICE),
    "PeaNut_body.png": ("reanim/Wallnut_body.png", PEA),
    "PeaNut_cracked1.png": ("reanim/Wallnut_cracked1.png", PEA),
    "PeaNut_cracked2.png": ("reanim/Wallnut_cracked2.png", PEA),
    "SpikyNut_body.png": ("reanim/Wallnut_body.png", SPIKY),
    "SpikyNut_cracked1.png": ("reanim/Wallnut_cracked1.png", SPIKY),
    "SpikyNut_cracked2.png": ("reanim/Wallnut_cracked2.png", SPIKY),
}


def read_original(data_dir, name):
    loose = data_dir / name
    if loose.is_file():
        return loose.read_bytes()
    pak_path = data_dir / "main.pak"
    with pak_path.open("rb") as f:
        records = pak.read_records(f)
        for record_name, size in records:
            if record_name.lower() == name.lower():
                return pak.read_xor(f, size)
            f.seek(size, io.SEEK_CUR)
    raise FileNotFoundError(f"{name} not found in {data_dir}")


def recolor(image, recipe):
    hue, sat_factor, value_factor = recipe
    alpha = image.getchannel("A")
    h, s, v = image.convert("RGB").convert("HSV").split()
    h = h.point(lambda _: hue)
    s = s.point(lambda x: min(255, int(x * sat_factor)))
    v = v.point(lambda x: min(255, int(x * value_factor)))
    out = Image.merge("HSV", (h, s, v)).convert("RGB")
    out.putalpha(alpha)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("data_dir", type=Path, help="game data folder (main.pak or extracted files)")
    parser.add_argument("output", type=Path, help="folder for the mod sprites")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    for out_name, (original, recipe) in MOD_IMAGES.items():
        image = Image.open(io.BytesIO(read_original(args.data_dir, original))).convert("RGBA")
        recolor(image, recipe).save(args.output / out_name)
    print(f"Wrote {len(MOD_IMAGES)} mod sprites to {args.output}")


if __name__ == "__main__":
    main()
