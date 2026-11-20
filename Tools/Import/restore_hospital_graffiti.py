"""Restore original wall graffiti with source-only interpolation and edge finishing."""
import hashlib
import json

import cv2
import numpy as np
from PIL import Image, ImageDraw

SCALE = 8
IDS = ("01", "03")
WEIGHTS = np.asarray((0.2126, 0.7152, 0.0722), dtype=np.float32)


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wrap_resize(values, size, method, pad=4):
    height, width, _ = values.shape
    if size[0] % width or size[1] % height:
        raise ValueError("Expected integer interpolation factors")
    sx, sy = size[0] // width, size[1] // height
    values = np.pad(values, ((pad, pad), (pad, pad), (0, 0)), mode="wrap")
    large = cv2.resize(values, (size[0] + pad * sx * 2, size[1] + pad * sy * 2), interpolation=method)
    return large[pad * sy:pad * sy + size[1], pad * sx:pad * sx + size[0]]


def pixel_round(values):
    return np.rint(np.clip(values, 0, 255)).astype(np.uint8)


def coarse_rgb(values, width, height):
    return values.reshape(height, SCALE, width, SCALE, 3).mean(axis=(1, 3))


def restore(pixels):
    h, w, _ = pixels.shape
    size = (w * SCALE, h * SCALE)
    smooth = cv2.bilateralFilter(pixels, 3, 8.0, 0.63)
    # Most structural information stays in the sampled source. A small bilateral
    # blend calms ring noise in flat paint without relaying marks or lines.
    stock_upsampled = wrap_resize(pixels, size, cv2.INTER_LANCZOS4)
    retouched = wrap_resize(smooth, size, cv2.INTER_LANCZOS4)
    rgb = 0.82 * stock_upsampled + 0.18 * retouched
    lum = rgb @ WEIGHTS
    narrow = cv2.GaussianBlur(lum, (0, 0), 1.7)
    medium = cv2.GaussianBlur(lum, (0, 0), 4.3)
    # Restore local acutance with bounded luma-only emphasis. Ambiguous low
    # contrasts receive little sharpening to avoid raising wall noise.
    short_edge = lum - narrow
    mid_edge = lum - medium
    taper = np.clip((np.abs(mid_edge) - 0.15) / 1.9, 0.0, 1.0)
    emphasis = np.clip(short_edge * 0.65 + mid_edge * 0.6, -8.5, 8.5) * taper
    rgb += emphasis[:, :, None]

    # Prevent interpolation/sharpening halos. The source samples themselves
    # define allowed brightness at every location.
    src_luma = pixels @ WEIGHTS
    kernel = np.ones((3, 3), dtype=np.uint8)
    low = cv2.resize(cv2.erode(src_luma, kernel), size, interpolation=cv2.INTER_LINEAR) - 2.0
    high = cv2.resize(cv2.dilate(src_luma, kernel), size, interpolation=cv2.INTER_LINEAR) + 2.0
    final_luma = rgb @ WEIGHTS
    rgb += (np.clip(final_luma, low, high) - final_luma)[:, :, None]

    # Keep the broad colour map anchored to the source while retaining
    # subpixel detail. Mild back-projection uses smooth errors, not a pixel grid.
    for _ in range(2):
        error = pixels - coarse_rgb(rgb, w, h)
        rgb += cv2.resize(error, size, interpolation=cv2.INTER_CUBIC) * 0.72
    return pixel_round(rgb)


def evidence(reference, image, destination):
    width, height = (1024, 512)
    margin = 16
    glyph = 30
    preview = Image.new("RGB", (2 * width + 3 * margin, height + glyph + 2 * margin), (28, 30, 34))
    before = reference.resize((width, height), Image.Resampling.BICUBIC)
    after = image.resize((width, height), Image.Resampling.LANCZOS)
    left, right = margin, width + 2 * margin
    drawing = ImageDraw.Draw(preview)
    drawing.text((left, margin), "Original 256 x 128, interpolated for comparison", fill="white")
    drawing.text((right, margin), "Source-faithful 2048 x 1024 restoration", fill="white")
    preview.paste(before, (left, glyph + margin))
    preview.paste(after, (right, glyph + margin))
    preview.save(destination, compress_level=2)


def run(origin, root):
    root.mkdir(parents=True, exist_ok=True)
    manifest, receipt = {}, {}
    for suffix in IDS:
        name = f"Prison_TEX_wallegout{suffix}.png"
        reference = origin / name
        with Image.open(reference) as source:
            if source.size != (256, 128):
                raise ValueError("Unexpected source UV tile dimensions")
            base = source.convert("RGB")
            alpha = source.convert("RGBA").getchannel("A")
        pixels = np.asarray(base, dtype=np.float32)
        restored = restore(pixels)
        img = Image.fromarray(restored)
        if alpha.getextrema() != (255, 255):
            img.putalpha(alpha.resize(img.size, Image.Resampling.NEAREST))
        filename = f"Prison_TEX_wallegout{suffix}_Restored_2048.png"
        path = root / filename
        img.save(path, compress_level=3)
        manifest[name] = filename
        evidence(base, img.convert("RGB"), root / f"Graffiti-{suffix}-Comparison.png")
        returned = coarse_rgb(restored.astype(np.float32), base.width, base.height)
        delta = returned - pixels
        structural = np.corrcoef((returned @ WEIGHTS).ravel(), (pixels @ WEIGHTS).ravel())[0, 1]
        broad = cv2.GaussianBlur(delta @ WEIGHTS, (0, 0), 8.0)
        receipt[name] = {
            "stock_sha256": file_hash(reference), "restored_sha256": file_hash(path),
            "stock_dimensions": list(base.size), "restored_dimensions": list(img.size),
            "source_alpha": "exact nearest expansion; opaque source stays opaque",
            "roundtrip_mean_rgb_error_0_255": round(float(np.abs(delta).mean()), 4),
            "roundtrip_mean_channel_bias": [round(float(x), 4) for x in delta.mean(axis=(0, 1))],
            "roundtrip_luma_correlation": round(float(structural), 6),
            "broad_roundtrip_luma_mae_0_255": round(float(np.abs(broad).mean()), 4),
        }
        print(name, receipt[name])
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (root / "restoration-qa.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
