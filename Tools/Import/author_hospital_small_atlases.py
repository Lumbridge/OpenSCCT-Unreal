"""Author the exterior wordmarks and the wall sensor's colour atlas."""

import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw

from author_hospital_room_signs import raster_label


def wordmarks(source):
    scale = 16
    plate = np.zeros((128 * scale, 128 * scale, 3), dtype=np.float32)
    fonts = Path("C:/Windows/Fonts")
    labels = (
        ("T Bell", (2, 1, 81, 24), (106, 175, 238), "arialbd.ttf"),
        ("WESTERN INVEST", (3, 28, 124, 45), (203, 50, 43), "impact.ttf"),
        ("TRANSGLOBAL", (5, 50, 122, 64), (221, 198, 114), "arialbd.ttf"),
    )
    for label, (x0, y0, x1, y1), color, font in labels:
        mask = np.zeros_like(plate[..., 0])
        mask[y0*scale:y1*scale, x0*scale:x1*scale] = raster_label(
            label, ((x1-x0)*scale, (y1-y0)*scale), fonts / font)
        glow = cv2.GaussianBlur(mask, (0, 0), scale * 0.95)
        layer = np.maximum(glow * 0.30, mask)[..., None] * np.asarray(color, dtype=np.float32)
        finish = source[max(0, y0-2):min(128, y1+2)]
        area = layer[max(0, y0-2)*scale:min(128, y1+2)*scale]
        gain = np.clip(finish.mean(axis=(0, 1)) / np.maximum(area.mean(axis=(0, 1)), 0.1), 0.55, 1.85)
        plate += layer * gain
    return Image.fromarray(np.rint(np.clip(plate, 0, 255)).astype(np.uint8))


def sensor():
    size = 16
    atlas = Image.new("RGB", (64 * size, 32 * size), "#272829")
    layout = ImageDraw.Draw(atlas)

    def panel(bounds, color, edge, bevel):
        coords = tuple(round(point * size) for point in bounds)
        layout.rounded_rectangle(coords, radius=round(bevel * size), fill=color, outline=edge, width=6)

    panel((0.4, 0.4, 63.6, 31.6), "#303234", "#595a57", 1.5)
    panel((0.7, 0.7, 11.8, 31.2), "#3e3f3c", "#191b1c", 1.0)
    panel((0.9, 1.0, 10.4, 9.1), "#231f1f", "#141517", 1.2)
    panel((12.3, 0.5, 27.6, 30.9), "#4d4c47", "#313233", 0.8)
    panel((30.4, 2.0, 62.6, 30.2), "#3d3e3e", "#1f2020", 0.9)
    panel((33.6, 5.0, 60.0, 27.0), "#575958", "#787772", 0.6)
    panel((34.4, 5.9, 59.1, 26.2), "#515452", "#414546", 0.35)
    for x, y in ((14.0, 2.1), (14.0, 28.7), (61.2, 3.4), (61.2, 28.6)):
        panel((x - 0.7, y - 0.7, x + 0.7, y + 0.7), "#63615b", "#2c2d2b", 1)
    panel((3.1, 2.8, 6.4, 6.2), "#bd231f", "#6a1115", 1.6)
    panel((4.2, 3.4, 5.6, 5.1), "#ed4a36", "#c53829", 0.8)
    panel((2.2, 27.1, 3.9, 29.0), "#c35132", "#803327", 0.45)
    return atlas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sign = "Prison_TEX_mh_enseigne.png"
    detector = "SGameplayObjectsTex_heat_defpas_lasertrig_blind.png"
    with Image.open(args.source / sign) as source:
        wordmarks(np.asarray(source.convert("RGB"), dtype=np.float32)).save(args.output / sign)
    with Image.open(args.source / detector) as source:
        original = np.asarray(source.convert("RGB"), dtype=np.float32)
    raster = np.asarray(sensor(), dtype=np.float32)
    offset = original.mean(axis=(0, 1)) - raster.mean(axis=(0, 1))
    Image.fromarray(np.rint(np.clip(raster + offset, 0, 255)).astype(np.uint8)).save(args.output / detector)
    (args.output / "manifest.json").write_text(json.dumps({sign: sign, detector: detector}, indent=2) + "\n")


if __name__ == "__main__":
    main()
