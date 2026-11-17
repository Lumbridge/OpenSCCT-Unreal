"""Makes the extra texture maps a converted OpenSCCT map lacks, before the Unreal import.

The original textures are colour only. Unreal's dynamic lighting needs surface detail, so this derives a
tangent-space normal map from each colour texture (its brightness read as height) and writes it to
<out>/<texture name>_N.png. Run with the system Python (needs numpy and Pillow):

    python prepare_textures.py <content>/svmap/<Map>/render <out folder>
"""
import os
import sys

import numpy as np
from PIL import Image

# The derived relief is deliberately gentle. A 2005 colour texture holds painted light, stains and dirt as well as
# real relief; read strongly as height, every stain becomes a lump and a wall looks like melted wax under a moving
# light. So: stains are taken out (only brightness changes a few texels wide count), every texture is brought to
# the same average slope (a noisy texture is not bumpier than a clean one), and the close-up detail is left to the
# CC0 detail layer of M_ScctSurface. Surface classes scale this map further ("macro_normal").
TARGET_SLOPE = 0.13   # the root-mean-square slope every map is brought to
MAX_GAIN = 2.0        # a nearly flat picture stays nearly flat: amplified, its noise reads as hammered metal
MAX_SIDE = 512        # small textures are derived at up to twice their size: smooth bumps, not bilinear blobs
# Bump this when the derivation changes: maps made by an older version are remade.
VERSION = "3"


def blur(h, passes):
    """A wrapping binomial blur (the textures tile)."""
    out = h
    for _ in range(passes):
        for axis in (0, 1):
            out = (np.roll(out, 1, axis=axis) + 2.0 * out + np.roll(out, -1, axis=axis)) * 0.25
    return out


def normal_from_colour(path):
    image = Image.open(path).convert("RGB")
    scale = 2 if max(image.size) * 2 <= MAX_SIDE else 1
    if scale > 1:
        image = image.resize((image.width * scale, image.height * scale), Image.BICUBIC)
    rgb = np.asarray(image, dtype=np.float32) / 255.0
    height = rgb @ np.array([0.299, 0.587, 0.114], dtype=np.float32)

    # Keep the band between single-texel noise and broad stains: joints, planks, rivets, cracks.
    smooth = blur(height, scale)
    relief = smooth - blur(smooth, 12 * scale)
    dx = (np.roll(relief, -1, axis=1) - np.roll(relief, 1, axis=1)) * 0.5 * scale
    dy = (np.roll(relief, -1, axis=0) - np.roll(relief, 1, axis=0)) * 0.5 * scale
    slope = float(np.sqrt(np.mean(dx * dx + dy * dy)))
    gain = min(MAX_GAIN, TARGET_SLOPE / max(slope, 1e-5))
    dx, dy = dx * gain, dy * gain
    # No single edge stands up like a cliff.
    limit = TARGET_SLOPE * 4.0
    dx, dy = np.clip(dx, -limit, limit), np.clip(dy, -limit, limit)

    # Unreal normal maps are DirectX style: green points down the image.
    normal = np.stack([-dx, dy, np.ones_like(dx)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return Image.fromarray(np.clip((normal * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB")


def main():
    render, out = sys.argv[1], sys.argv[2]
    textures = os.path.join(render, "textures")
    os.makedirs(out, exist_ok=True)
    stamp = os.path.join(out, "version.txt")
    current = os.path.exists(stamp) and open(stamp).read().strip() == VERSION
    made = 0
    for name in sorted(os.listdir(textures)):
        stem, extension = os.path.splitext(name)
        # Lightmap pages are baked light, not surfaces; flipbook sheets are effects.
        if extension.lower() != ".png" or "_lightmap" in stem or stem.endswith("__flip"):
            continue
        target = os.path.join(out, stem + "_N.png")
        source = os.path.join(textures, name)
        if current and os.path.exists(target) and os.path.getmtime(target) >= os.path.getmtime(source):
            continue
        normal_from_colour(source).save(target)
        made += 1
    with open(stamp, "w") as f:
        f.write(VERSION)
    print("normal maps written: %d (in %s)" % (made, out))


if __name__ == "__main__":
    main()
