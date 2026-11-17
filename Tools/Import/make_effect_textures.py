"""Draws the few textures the effects need that are not original game content. Runs in the system Python
(numpy + Pillow, as prepare_textures.py):

    python make_effect_textures.py <output folder>

  T_RainStreak.png  one falling raindrop as the eye sees it: a thin streak, bright in the middle, fading to both
                    ends and to its sides. White with the shape in the alpha channel.

  T_RainCoverNone.png  the cover picture of a rain volume that has none (prepare_rain_cover.py makes the real ones
                    per map): one grey value, "nothing stands in the rain's way". It is what the rain and wet
                    ground materials hold until a map gives them a picture.

import_effects.py imports the results to /Game/OpenSCCT/Effects (committed: they are made here, see docs/ASSETS.md).
"""
import os
import sys

import numpy as np
from PIL import Image


def rain_streak(width=16, height=128):
    across = np.linspace(-1.0, 1.0, width)[None, :]
    along = np.linspace(0.0, 1.0, height)[:, None]
    # Narrow across; along, a quick rise at the leading (lower) end and a long tail above it.
    shape = np.exp(-(across * 2.6) ** 2) * np.clip(along / 0.15, 0.0, 1.0) * (1.0 - along) ** 0.6
    alpha = np.clip(shape / shape.max(), 0.0, 1.0)
    pixels = np.dstack([np.full_like(alpha, 1.0)] * 3 + [alpha])
    return Image.fromarray((pixels * 255.0 + 0.5).astype(np.uint8), "RGBA")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rain_streak().save(os.path.join(out, "T_RainStreak.png"))
    Image.fromarray(np.zeros((4, 4), dtype=np.uint16)).save(os.path.join(out, "T_RainCoverNone.png"))
    print("effect textures written to " + out)


main()
