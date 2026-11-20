"""Rebuild Mount Hospital's vending colour atlas from the map owner's original textures."""
import argparse
import filecmp
import hashlib
import os
from pathlib import Path
import tempfile

import numpy as np
from PIL import Image, ImageFilter


SCALE = 8
REGIONS = (
    ("cabinet", "Cabinet-Art.png", (6, 7, 121, 211), (0.07, 0.0, 0.93, 1.0), 1.0),
    ("left shelf column", "Shelf-Art.png", (139, 6, 183, 204), (0.0, 0.0, 0.437, 1.0), 1.22),
    ("right shelf column", "Shelf-Art.png", (197, 6, 247, 204), (0.586, 0.0, 1.0, 1.0), 1.22),
    ("selector keys", "Panel-Art.png", (129, 213, 199, 254), (0.001, 0.004, 0.727, 0.996), 1.0),
    ("payment controls", "Panel-Art.png", (202, 211, 254, 250), (0.727, 0.004, 0.998, 0.996), 1.1),
)
SOURCE_NAMES = ("Prison_TEX_machineEMF.png", "Prison_TEX_machine.png")
OUTPUT_NAME = "Prison_TEX_machine_HD.png"
STOCK_RGBA_SHA256 = "62a02b728974f82a5448bf333f9bf4a8e15e8bdfe754c92d5f709d82885524bf"
PLATE_HASHES = {
    "Cabinet-Art.png": "b54a452ac3921eace23bd1bd9113d0426cc1add58a47105e77a8543c5d08e08b",
    "Shelf-Art.png": "e4d29305a036be84b78dcfc63e16b15cb33633e5b5f41f114e99eff0015cfbfe",
    "Panel-Art.png": "8bc3d4b185ccaaf7d72d7e1445007b2ea4b6c25927479488469f1274e01a0458",
}


def box_pixels(box, factor):
    return tuple(round(v * factor) for v in box)


def resize_lanczos(image, size):
    # The approved atlas samples horizontally first. Some Pillow versions choose
    # the pass order by aspect ratio, which changes rounding on the payment panel.
    rows = image.resize((size[0], image.height), Image.Resampling.LANCZOS)
    return rows.resize(size, Image.Resampling.LANCZOS)


def blend_plate(target, original, source, box, crop, power):
    left, top, right, bottom = box_pixels(box, SCALE)
    width, height = right - left, bottom - top
    x1, y1, x2, y2 = crop
    source_box = (round(x1 * source.width), round(y1 * source.height), round(x2 * source.width), round(y2 * source.height))
    plate = resize_lanczos(source.crop(source_box), (width, height))
    samples = (np.asarray(plate, dtype=np.float32) / 255.0) ** power * 255.0
    old_mean = np.asarray(original.crop(box), dtype=np.float32).mean(axis=(0, 1))
    # Restore new physical detail while retaining the source material area's
    # overall display-space colour and brightness under its existing light.
    gain = old_mean / np.maximum(samples.mean(axis=(0, 1)), 1.0)
    new = np.rint(np.clip(samples * gain, 0.0, 255.0)).astype(np.uint8)
    y, x = np.mgrid[0:height, 0:width]
    distance = np.minimum(np.minimum(x, width - 1 - x), np.minimum(y, height - 1 - y))
    feather = np.rint(np.minimum(distance / 4.0, 1.0) * 255.0).astype(np.uint8)
    target.paste(Image.fromarray(new), (left, top), Image.fromarray(feather))



def plate_grid(box):
    target = box_pixels(box, SCALE)
    width, height = target[2] - target[0], target[3] - target[1]
    y, x = np.mgrid[0:height, 0:width]
    coords = box[0] + (x + 0.5) / SCALE, box[1] + (y + 0.5) / SCALE
    return target, coords


def finish_metal(target, original, art, box, profile):
    dest, (x, y) = plate_grid(box)
    width, height = dest[2] - dest[0], dest[3] - dest[1]
    sample = np.asarray(resize_lanczos(art.convert("L"), (width, height)), dtype=np.float32)
    luma = np.clip(profile(x, y) + 0.25 * (sample - np.mean(sample)), 0.0, 255.0)
    old_mean = np.asarray(original.crop(box), dtype=np.float32).mean(axis=(0, 1))
    chroma = old_mean / max(float(np.mean(luma)), 1.0)
    plate = np.rint(np.clip(luma[:, :, None] * chroma, 0.0, 255.0)).astype(np.uint8)
    target.paste(Image.fromarray(plate), dest[:2])


def louvers(x, y):
    base = np.full(x.shape, 15.0)
    for center in (215.25, 220.25, 225.25, 229.25, 234.25, 239.25, 244.25, 249.25):
        d = y - center
        slot = 210.0 * np.exp(-(d / 0.68) ** 2) + 88.0 * np.exp(-((d + 0.96) / 0.88) ** 2)
        base = np.maximum(base, np.minimum(slot + 16.0, 239.0))
    return base


def rounded_distance(x, y, cx, cy, half_width, half_height, corner):
    dx = np.abs(x - cx) - half_width + corner
    dy = np.abs(y - cy) - half_height + corner
    return np.hypot(np.maximum(dx, 0.0), np.maximum(dy, 0.0)) + np.minimum(np.maximum(dx, dy), 0.0) - corner


def perforated_rail(x, y):
    # The original atlas compresses this rivet rail vertically and the face
    # becomes darker near the bottom. Keep that broad variation for parity.
    base = 199.0 - 75.0 * np.clip((y - 72.0) / 65.0, 0.0, 1.0)
    base += 19.0 * np.exp(-((x - 184.1) / 0.58) ** 2)
    base -= 42.0 * np.exp(-((x - 196.7) / 0.64) ** 2)
    for lo, hi in ((9, 12), (26, 29), (44, 47), (65, 70), (87, 90), (104, 108),
                   (122, 126), (138, 143), (158, 162), (174, 179), (190, 194)):
        distance = rounded_distance(x, y, 191.0, (lo + hi + 1) / 2, 3.1, (hi - lo + 1) / 2, 0.7)
        dark = np.clip((0.16 - distance) / 0.31, 0.0, 1.0)
        bevel = np.exp(-((distance - 0.1) / 0.35) ** 2)
        base = base * (1.0 - dark) + 9.0 * dark + 16.0 * bevel * (1.0 - dark)
    return base


def read_original(texture_dir):
    for name in SOURCE_NAMES:
        with Image.open(texture_dir / name) as source:
            if source.size != (256, 256):
                raise ValueError(f"{name} must be the original 256 x 256 Mount Hospital atlas.")
            rgba = source.convert("RGBA")
            if hashlib.sha256(rgba.tobytes()).hexdigest() != STOCK_RGBA_SHA256:
                raise ValueError(f"{name} has different colour or alpha data from the supported Mount Hospital atlas.")
    return rgba


def build(map_dir, plates, output):
    stock = read_original(map_dir / "render/textures")
    sources = {(map_dir / "render/textures" / name).resolve() for name in SOURCE_NAMES}
    for name, expected in PLATE_HASHES.items():
        path = plates / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Unexpected vending artwork in {path}.")
        sources.add(path.resolve())
    if output.resolve() in sources:
        raise ValueError("Choose an output file separate from the input textures.")
    size = box_pixels(stock.size, SCALE)
    alpha = stock.getchannel("A").resize(size, Image.Resampling.NEAREST)
    original = stock.convert("RGB")
    atlas = resize_lanczos(original, size)
    atlas = atlas.filter(ImageFilter.UnsharpMask(radius=2.0, percent=60, threshold=2))
    for _, filename, box, crop, power in REGIONS:
        with Image.open(plates / filename) as source:
            blend_plate(atlas, original, source.convert("RGB"), box, crop, power)
    with Image.open(plates / "Cabinet-Art.png") as art:
        finish_metal(atlas, original, art, (4, 214, 123, 252), louvers)
        finish_metal(atlas, original, art, (183, 5, 197, 205), perforated_rail)
    atlas.putalpha(alpha)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + ".", suffix=".tmp", delete=False) as pending:
        temporary = Path(pending.name)
    try:
        atlas.save(temporary, format="PNG", compress_level=3)
        try:
            os.replace(temporary, output)
        except PermissionError:
            # A game may already be reading the identical image published by a
            # concurrent launch. Windows can hold that file open through its read.
            if not output.is_file() or not filecmp.cmp(temporary, output, shallow=False):
                raise
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path, help="converted svmap/MounE directory")
    parser.add_argument("--plates", type=Path, default=Path(__file__).resolve().parents[2] / "Art/Maps/MounE/Vending",
                        help="directory containing the three original vending artwork plates")
    parser.add_argument("--output", type=Path, help="PNG destination (defaults to the map's render/overrides directory)")
    args = parser.parse_args()
    output = args.output or args.map / "render/overrides" / OUTPUT_NAME
    try:
        build(args.map, args.plates, output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Could not build vending override: {error}\n")
    print(output)
