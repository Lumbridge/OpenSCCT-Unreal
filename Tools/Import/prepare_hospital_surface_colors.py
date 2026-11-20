"""Build replacement colour artwork for the hospital's structural surfaces."""

import argparse
import json
from pathlib import Path
from typing import NamedTuple

import cv2
import numpy as np
from PIL import Image


class Finish(NamedTuple):
    tile: str
    amplitude: float
    grain_scale: float
    clean: float


CONCRETE = Finish("CastConcreteMicrodetail.png", 7.0, 1.0, 0.72)
MASONRY = Finish("MasonryMicrodetail.png", 6.0, 0.85, 0.67)
PLASTER = Finish("HospitalPaint.png", 2.5, 1.4, 0.76)
PAINTED_STEEL = Finish("PowderCoatMicrodetail.png", 3.1, 1.25, 0.46)
STEEL = Finish("SatinSteelFineDetail.png", 3.6, 0.95, 0.54)
WOOD = Finish("HospitalOak.png", 5.0, 0.8, 0.62)
RUBBER = Finish("WornTreadRubberFineDetail.png", 3.2, 1.0, 0.53)
GLASS = Finish("GlassMicroabrasion.png", 2.0, 1.0, 0.61)
POLYMER = Finish("MoldedPolymerMicrodetail.png", 3.0, 1.2, 0.68)

# Texture coordinates and broad colour remain authored by the map. Mixed atlases use
# independently painted layers (e.g. CRT glass and painted housing), not one global detail texture.
BASES = {
    CONCRETE: (
        "Box", "FLO_concrete01_2", "FLO_concrete02b", "WAL_concrete01_2",
        "WAL_cretedirty01", "WAL_wall08", "betonBloc_ABA", "brick01", "brick02", "escaliers",
    ),
    MASONRY: (
        "WAL_brick01b", "WAL_brick07b", "WAL_defaultrebricks01b", "WAL_defaultrebricks01c",
        "wallbrick01", "wallegout04",
    ),
    PLASTER: (
        "FLO1_lvl1", "Foplaf", "Foplaf_Txt", "WAL_newtech05", "doucheroom", "murplaco",
        "transitions", "wall_lvl1", "wallfloor1",
    ),
    PAINTED_STEEL: (
        "CIA_chainfence", "DOO_elevator01", "conduit", "grilleair", "grillevitres", "metalgrille",
        "mh_citern", "mh_esc_ramp", "rembarde", "roomdoor", "tableroule",
    ),
    STEEL: ("ACIER", "Tole01_Aba", "etagere", "pillon1"),
    WOOD: ("ACC_crate01", "Palette", "woodpanels_Aba", "woodplank"),
    RUBBER: ("mh_esc_march",),
    GLASS: ("mh_glass", "win_05"),
    POLYMER: ("conesecu", "moa_cable", "sac", "tissu1"),
}

# Bounds are normalised source UV rectangles and refer to an independently chosen finish.
ISLANDS = {
    "Box": [((0, 0.49, 1, 1), STEEL)],
    "escaliers": [((0, 0, 1, 0.16), PAINTED_STEEL), ((0, 0.88, 1, 1), PAINTED_STEEL)],
    "rembarde": [((0, 0.67, 1, 1), CONCRETE)],
    "roomdoor": [((0.57, 0.02, 0.85, 0.19), STEEL)],
    "win_05": [((0, 0, 1, 1), GLASS)],
}


def open_rgb(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=np.float32)


def guide_for(image):
    """Separate surviving material structure from coarse baked surface grain."""
    # The range guidance protects joints, painted stripes, openings and UV islands.
    smooth = cv2.bilateralFilter(image, 13, 25.0, 2.0)
    luma = image @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    broad = cv2.GaussianBlur(luma, (0, 0), 0.8)
    gradient_x = cv2.Sobel(broad, cv2.CV_32F, 1, 0, scale=0.125)
    gradient_y = cv2.Sobel(broad, cv2.CV_32F, 0, 1, scale=0.125)
    edge = np.clip((np.hypot(gradient_x, gradient_y) - 7.0) / 19.0, 0, 1)
    edge = cv2.GaussianBlur(cv2.dilate(edge, np.ones((3, 3), dtype=np.uint8)), (0, 0), 0.65)
    return smooth, edge


def periodic_detail(path):
    pixels = open_rgb(path)
    channels = pixels @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    field = channels - cv2.GaussianBlur(channels, (0, 0), 29.0)
    scale = max(float(np.percentile(np.abs(field), 95)), 1.0)
    return np.clip(field / scale, -1.8, 1.8)


def map_tile(field, width, height, scale):
    # Filter periodic texture coordinates from the scanner's central square. Reflecting
    # the grid at the UV edge makes the boundaries meet for a repeating map material.
    aspect = max(width, height)
    size = min(field.shape) - 12
    inset_y = (field.shape[0] - size) / 2
    inset_x = (field.shape[1] - size) / 2
    u = ((np.arange(width, dtype=np.float32) + 0.5) / width)
    v = ((np.arange(height, dtype=np.float32) + 0.5) / height)
    uv_width = min(1.0, width / aspect * scale)
    uv_height = min(1.0, height / aspect * scale)
    u = inset_x + size * (0.5 + (abs(2 * u - 1) - 0.5) * uv_width)
    v = inset_y + size * (0.5 + (abs(2 * v - 1) - 0.5) * uv_height)
    map_x, map_y = np.meshgrid(u, v)
    painted = cv2.remap(field, map_x, map_y, cv2.INTER_LANCZOS4)
    return painted - painted.mean()


def reconstruct(source, finish, lookup, size, islands):
    clean, edge = guide_for(source)
    width, height = size
    raw = cv2.resize(source, size, interpolation=cv2.INTER_CUBIC)
    smooth = cv2.resize(clean, size, interpolation=cv2.INTER_CUBIC)
    protect = np.clip(cv2.resize(edge, size, interpolation=cv2.INTER_LINEAR), 0, 1)
    writable = (1 - protect) * np.clip(raw.max(axis=2) / 20, 0, 1)

    # Fill the actual RGB image. Preserve protected profiles and replace coarse colour
    # variance under each finish with the corresponding new photographed surface.
    def paint(kind):
        field = map_tile(lookup[kind.tile], width, height, kind.grain_scale)
        return (smooth - raw) * kind.clean + field[..., None] * kind.amplitude

    change = paint(finish)
    for bounds, kind in islands:
        u0, v0, u1, v1 = bounds
        x0, x1 = round(u0 * width), round(u1 * width)
        y0, y1 = round(v0 * height), round(v1 * height)
        change[y0:y1, x0:x1] = paint(kind)[y0:y1, x0:x1]

    change *= writable[..., None]
    # Keep aggregate RGB of each source. The protected portion is never altered by the
    # colour compensation, so paint markings and deep inset gaps keep their contrast.
    change -= writable[..., None] * (change.sum(axis=(0, 1)) / max(float(writable.sum()), 1.0))
    return np.rint(np.clip(raw + change, 0, 255)).astype(np.uint8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("tiles", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("the output directory must be separate from the original map artwork")
    lookup = {finish.tile: periodic_detail(args.tiles / finish.tile) for finish in BASES}
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {}
    evidence = {}
    for finish, names in BASES.items():
        for name in names:
            file = f"Prison_TEX_{name}.png"
            original = open_rgb(args.source / file)
            height, width = original.shape[:2]
            side = max(width, height)
            target = 512 if side < 64 else 2048
            scale = max(2, target // side)
            size = width * scale, height * scale
            output = reconstruct(original, finish, lookup, size, ISLANDS.get(name, []))
            Image.fromarray(output).save(args.output / file)
            reference = cv2.resize(original, size, interpolation=cv2.INTER_CUBIC)
            evidence[file] = {
                "size": size,
                "finish": finish.tile,
                "source": [width, height],
                "mean_rgb_change": np.round(output.mean(axis=(0, 1)) - original.mean(axis=(0, 1)), 3).tolist(),
                "mean_difference_from_cubic_source": round(float(np.abs(output - reference).mean()), 3),
            }
            manifest[file] = file
            print(f"{file}: {width}x{height} to {size[0]}x{size[1]} from {finish.tile}")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (args.output / "surface-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
