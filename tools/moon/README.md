# Moon images

The moon screen shows the Moon in its current phase on a black background. The images are rendered
by NASA's Scientific Visualization Studio, from
[Moon Phase and Libration, 2026](https://svs.gsfc.nasa.gov/5587/) (NASA/SVS, public domain). They
live on the device's microSD card.

## Preparing the images

```bash
python3 tools/moon/prepare.py
```

Requires Python 3 with Pillow and NumPy, plus `curl`. The script:

1. Downloads NASA's hourly moon data for 2026 and picks 60 frames spread evenly over the lunation
   starting on 11 Sep 2026, so there's one frame about every 12 hours of the cycle.
2. Crops the disc out of each 730×730 frame and scales it to 440px across. NASA's frames change
   size with the Moon's distance; here every frame has the same size and sits in the centre.
3. Dithers each frame to black and white. The sunlit side uses Atkinson dithering, which keeps the
   craters crisp. The unlit side, faintly lit by earthshine in NASA's frames, becomes a sparse random
   scatter of dots (at most 10%), so it reads as very dark rather than pitch black.

The prepared images are committed in `tools/moon/images/`, so you only need to run the script to change
how they look:

- `images/00.bin` … `images/59.bin`: the files for the SD card. Frame `NN` shows phase `NN / 60`, where
  0 is new moon and 0.5 is full, which is how Open-Meteo reports `moon_phase`. Each file is `MOON`,
  width and height as little-endian 16-bit values, then 1-bit rows, most significant bit first,
  with 1 meaning lit.
- `out/preview/NN.png` (not committed): each frame as the panel shows it; `out/preview/sheet.png`:
  all 60 at a glance.

Downloads are cached in `tools/moon/cache/`, so tweaking the tone curve or dithering at the top of
the script and running it again is quick.

## Copying them to the SD card

Upload `tools/moon/images` to `/moon` on the card with the SD uploader. The moon screen reads from
that folder, keeping the images apart from anything else on the card:

```bash
~/.platformio/penv/bin/python tools/sd-upload/upload.py /dev/cu.usbserial-10 tools/moon/images /moon
``` See
[tools/sd-upload/README.md](../sd-upload/README.md).
