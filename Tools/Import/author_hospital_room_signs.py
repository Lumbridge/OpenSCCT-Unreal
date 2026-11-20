"""Draw original HD sign lettering within the converted Hospital UV islands."""

import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def raster_label(text, size, font_path):
    font = ImageFont.truetype(str(font_path), 128)
    bounds = font.getbbox(text)
    mask = Image.new("L", (bounds[2] - bounds[0], bounds[3] - bounds[1]))
    ImageDraw.Draw(mask).text((-bounds[0], -bounds[1]), text, font=font, fill=255)
    return np.asarray(mask.crop(mask.getbbox()).resize(size, Image.Resampling.LANCZOS),
                      dtype=np.float32) / 255


def linear(value):
    srgb = np.clip(value, 0, 255) / 255
    return np.where(srgb < 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def scaled(bounds, scale):
    return tuple(round(coord * scale) for coord in bounds)


def sign_layer(source, rgb, alpha, item, scale, font_path):
    text, cell, ink = item
    x0, y0, x1, y1 = cell
    stock = source[y0:y1, x0:x1]
    old_rgb, old_alpha = stock[..., :3], stock[..., 3]
    brightness = np.max(old_rgb, axis=2)
    highlights = brightness >= np.percentile(brightness, 96)
    dark = brightness <= np.percentile(brightness, 25)
    background = np.median(old_rgb[dark], axis=0)
    foreground = np.maximum(np.median(old_rgb[highlights], axis=0), background + 1)
    alpha_floor = min(float(np.percentile(old_alpha, 10)), 12.0)

    left, top, right, bottom = scaled(cell, scale)
    gx0, gy0, gx1, gy1 = scaled(ink, scale)
    mask = np.zeros((bottom - top, right - left), dtype=np.float32)
    mask[gy0-top:gy1-top, gx0-left:gx1-left] = raster_label(text, (gx1-gx0, gy1-gy0), font_path)
    halo = cv2.GaussianBlur(mask, (0, 0), scale * 1.1)
    shade = np.maximum(mask, halo * 0.18)
    paint = background + shade[..., None] * (foreground - background)

    # Preserve total glow per sign. The new high-resolution lettering and its mask
    # share the same raster; keeping the old mask here would cut through the new strokes.
    old_energy = float((linear(old_rgb).mean(axis=2) * old_alpha).mean())
    response = linear(paint).mean(axis=2)
    ink_alpha = np.maximum(mask, halo * 0.28)
    factor = (old_energy - float(response.mean()) * alpha_floor) / max(float((response * ink_alpha).mean()), 1e-7)
    emission = np.clip(alpha_floor + ink_alpha * factor, 0, 255)
    if emission.max() >= 254:
        # A small soft edge gives over-range signs room to keep their original illumination.
        ink_alpha = np.maximum(mask, halo * 0.55)
        low, high = 0.0, 2048.0
        for _ in range(16):
            mid = 0.5 * (low + high)
            trial = np.clip(alpha_floor + ink_alpha * mid, 0, 255)
            if float((trial * response).mean()) < old_energy:
                low = mid
            else:
                high = mid
        emission = np.clip(alpha_floor + ink_alpha * high, 0, 255)

    region = (slice(top, bottom), slice(left, right))
    yy, xx = np.indices(mask.shape, dtype=np.float32)
    edge = np.minimum.reduce((xx, yy, mask.shape[1] - 1 - xx, mask.shape[0] - 1 - yy))
    blend = np.clip(edge / (2 * scale), 0, 1)
    rgb[region] += (paint - rgb[region]) * blend[..., None]
    alpha[region] += (emission - alpha[region]) * blend
    return [round(old_energy, 5), round(float((linear(rgb[region]).mean(axis=2) * alpha[region]).mean()), 5)]


def build(source_dir, output, fonts):
    definitions = []
    for level in (1, 2):
        labels = []
        for index in range(24):
            x, y = (index % 4) * 64, (index // 4) * 32
            labels.append((str(level * 100 + index), (x, y, x + 64, y + 32), (x + 10, y + 7, x + 54, y + 24)))
        if level == 2:
            labels.append(("Nursery", (145, 194, 242, 226), (150, 198, 237, 222)))
        definitions.append((f"MHOtex_xbox_mh_numlvl{level}", 8, fonts / "consolab.ttf", labels))
    definitions.append(("MHOtex_xbox_psyEMF", 4, fonts / "arial.ttf", [
        ("PSYCHIATRIC SERVICE", (24, 9, 493, 80), (45, 23, 464, 64)),
    ]))
    facts = {}
    map_colors = {}
    for stem, scale, font, labels in definitions:
        png = stem + ".png"
        with Image.open(source_dir / png) as image:
            source = np.asarray(image.convert("RGBA"), dtype=np.float32)
        height, width = source.shape[:2]
        target = (width * scale, height * scale)
        rgb = cv2.resize(source[..., :3], target, interpolation=cv2.INTER_CUBIC)
        alpha = cv2.resize(source[..., 3], target, interpolation=cv2.INTER_LINEAR)
        energies = {item[0]: sign_layer(source, rgb, alpha, item, scale, font) for item in labels}
        color = np.rint(np.clip(rgb, 0, 255)).astype(np.uint8)
        rgba = np.dstack((color, np.rint(np.clip(alpha, 0, 255)).astype(np.uint8)))
        mask_stem = "MHOtex_xbox_psyservice" if stem == "MHOtex_xbox_psyEMF" else stem
        mask_name = mask_stem + "_SelfIllumMask.png"
        Image.fromarray(color).save(output / png)
        Image.fromarray(rgba).save(output / mask_name)
        map_colors[png] = png
        facts[mask_name] = {"size": target, "per_label_glow_mean_before_after": energies}
    (output / "manifest.json").write_text(json.dumps(map_colors, indent=2) + "\n", encoding="utf-8")
    (output / "mask-art.json").write_text(json.dumps(facts, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fonts", type=Path, default=Path("C:/Windows/Fonts"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    build(args.source, args.output, args.fonts)


if __name__ == "__main__":
    main()
