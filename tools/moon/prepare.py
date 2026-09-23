#!/usr/bin/env python3
"""Prepares NASA moon images for the moon screen.

Picks FRAME_COUNT frames spread evenly over one lunation from NASA's "Moon Phase and Libration, 2026"
(SVS 5587), normalizes them to the same disc size, and dithers them to black and white for the panel.

Writes images/NN.bin for the SD card (committed, so the card can be filled without rerunning this) and
out/preview/*.png to check the result. Frame NN shows the
moon at phase NN / FRAME_COUNT, where 0 is new moon and 0.5 is full, matching Open-Meteo's moon_phase.

Bin format: "MOON", width and height as little-endian uint16, then rows of 1-bit pixels, most
significant bit first, 1 = lit.
"""
import json
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

SVS = "https://svs.gsfc.nasa.gov/vis/a000000/a005500/a005587"
FRAMES = f"{SVS}/frames/730x730_1x1_30p"
MOONINFO = f"{SVS}/mooninfo_2026.json"

FRAME_COUNT = 60
LUNATION_START = "11 Sep 2026"  # The new moon that starts the lunation the frames come from
DISC = 440  # Moon diameter on the panel, which is 480 pixels tall
SIZE = 448  # Bitmap edge, a multiple of 8 so rows pack into whole bytes

# Measured on a full moon frame: the disc is centred in the 730px frame at this scale
FRAME_CENTER = (364.5, 365.0)
PIXELS_PER_ARCSEC = 656 / 1873.7

# Tone curve before dithering: levels at or below BLACK stay black, so the faint earthshine on the
# unlit side becomes a sparse ghost of the disc rather than noise, and GAMMA lifts the grey maria
BLACK = 8
WHITE = 235
GAMMA = 0.85

HERE = Path(__file__).parent
CACHE = HERE / "cache"
IMAGES = HERE / "images"
OUT = HERE / "out"


def fetch(url, path):
    # curl rather than urllib, which stalls on NASA's server
    if not path.exists():
        subprocess.run(["curl", "--fail", "--silent", "--show-error", "--max-time", "120", "-o", path, url], check=True)
    return path


def pick_frames(info):
    """Returns (frame number, phase fraction) for evenly spaced phases across one lunation."""
    start = next(i for i, entry in enumerate(info) if entry["time"].startswith(LUNATION_START) and entry["age"] < 1)
    end = next(i for i in range(start + 1, len(info)) if info[i]["age"] < info[i - 1]["age"])
    length = info[end - 1]["age"] + 1 / 24

    picks = []
    for k in range(FRAME_COUNT):
        target = k / FRAME_COUNT
        hour = min(range(start, end), key=lambda i: abs(info[i]["age"] / length - target))
        picks.append((hour + 1, info[hour]))
    return picks


def normalize(frame, diameter_arcsec):
    """Crops the disc out of a NASA frame and scales it to the same size on every frame."""
    radius = diameter_arcsec * PIXELS_PER_ARCSEC / 2
    scale = (DISC / 2) / radius
    half = SIZE / 2 / scale
    cx, cy = FRAME_CENTER
    box = (cx - half, cy - half, cx + half, cy + half)
    return frame.convert("L").resize((SIZE, SIZE), Image.LANCZOS, box=box)


def atkinson(gray):
    """Atkinson dithering: passes on only 3/4 of the error, so dark areas stay clean and highlights crisp."""
    pixels = gray.astype(np.float32) / 255
    height, width = pixels.shape
    out = np.zeros((height, width), dtype=bool)
    neighbours = ((0, 1), (0, 2), (1, -1), (1, 0), (1, 1), (2, 0))
    for y in range(height):
        for x in range(width):
            value = pixels[y, x]
            lit = value >= 0.5
            out[y, x] = lit
            error = (value - lit) / 8
            for dy, dx in neighbours:
                ny, nx = y + dy, x + dx
                if ny < height and 0 <= nx < width:
                    pixels[ny, nx] += error
    return out


def tone(gray):
    values = np.clip((gray.astype(np.float32) - BLACK) / (WHITE - BLACK), 0, 1) ** GAMMA
    return (values * 255).astype(np.uint8)


def main():
    CACHE.mkdir(exist_ok=True)
    IMAGES.mkdir(exist_ok=True)
    (OUT / "preview").mkdir(parents=True, exist_ok=True)

    info = json.loads(fetch(MOONINFO, CACHE / "mooninfo_2026.json").read_text())
    sheet = Image.new("L", (10 * SIZE // 4, 6 * SIZE // 4))

    for k, (frame_number, entry) in enumerate(pick_frames(info)):
        name = f"moon.{frame_number:04d}.jpg"
        frame = Image.open(fetch(f"{FRAMES}/{name}", CACHE / name))
        lit = atkinson(tone(np.asarray(normalize(frame, entry["diameter"]))))

        # Nothing may be lit outside the disc, whatever the dithering spilled
        yy, xx = np.mgrid[0:SIZE, 0:SIZE]
        lit &= (xx - SIZE / 2 + 0.5) ** 2 + (yy - SIZE / 2 + 0.5) ** 2 <= (DISC / 2 + 1) ** 2

        header = b"MOON" + struct.pack("<HH", SIZE, SIZE)
        (IMAGES / f"{k:02d}.bin").write_bytes(header + np.packbits(lit, axis=1).tobytes())

        # Full panel preview: black screen with the moon centred, as the device draws it
        panel = Image.new("L", (800, 480))
        panel.paste(Image.fromarray(lit.astype(np.uint8) * 255), ((800 - SIZE) // 2, (480 - SIZE) // 2))
        panel.save(OUT / "preview" / f"{k:02d}.png")
        thumb = Image.fromarray(lit.astype(np.uint8) * 255).resize((SIZE // 4, SIZE // 4), Image.BOX)
        sheet.paste(thumb, ((k % 10) * SIZE // 4, (k // 10) * SIZE // 4))
        print(f"{k:02d}.bin  phase {k / FRAME_COUNT:.3f}  from {name} ({entry['time']}, {entry['phase']:.0f}% lit)")

    sheet.save(OUT / "preview" / "sheet.png")


if __name__ == "__main__":
    main()
