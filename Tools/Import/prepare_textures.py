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

# How strongly brightness differences become slope. Higher is bumpier.
STRENGTH = 2.5


def normal_from_colour(path):
    image = Image.open(path).convert("RGB")
    rgb = np.asarray(image, dtype=np.float32) / 255.0
    height = rgb @ np.array([0.299, 0.587, 0.114], dtype=np.float32)

    # Two scales: fine grain plus the broader shapes (bricks, panels), both wrapping as the textures tile.
    def slopes(h):
        dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
        dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
        return dx, dy

    def blur(h):
        out = h.copy()
        for axis in (0, 1):
            out = (np.roll(out, 1, axis=axis) + 2.0 * out + np.roll(out, -1, axis=axis)) * 0.25
        return out

    fine_x, fine_y = slopes(height)
    broad = blur(blur(blur(height)))
    broad_x, broad_y = slopes(broad)
    dx = (fine_x * 0.6 + broad_x * 1.8) * STRENGTH
    dy = (fine_y * 0.6 + broad_y * 1.8) * STRENGTH

    # Unreal normal maps are DirectX style: green points down the image.
    normal = np.stack([-dx, dy, np.ones_like(dx)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return Image.fromarray(np.clip((normal * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB")


def main():
    render, out = sys.argv[1], sys.argv[2]
    textures = os.path.join(render, "textures")
    os.makedirs(out, exist_ok=True)
    made = 0
    for name in sorted(os.listdir(textures)):
        stem, extension = os.path.splitext(name)
        # Lightmap pages are baked light, not surfaces; flipbook sheets are effects.
        if extension.lower() != ".png" or "_lightmap" in stem or stem.endswith("__flip"):
            continue
        target = os.path.join(out, stem + "_N.png")
        source = os.path.join(textures, name)
        if os.path.exists(target) and os.path.getmtime(target) >= os.path.getmtime(source):
            continue
        normal_from_colour(source).save(target)
        made += 1
    print("normal maps written: %d (in %s)" % (made, out))


if __name__ == "__main__":
    main()
