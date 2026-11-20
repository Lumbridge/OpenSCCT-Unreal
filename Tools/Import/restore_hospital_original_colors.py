"""Resolve the Hospital's original colour artwork at a higher texel density."""

import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def open_rgba(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGBA"))


def deblock_axis(image):
    result = image.copy()
    # At a DXT seam only, reduce isolated low-amplitude steps that are not
    # supported by a gradient on either side. Large design edges are ineligible.
    columns = np.arange(4, image.shape[1] - 1, 4)
    middle = image[:, columns] - image[:, columns - 1]
    outer = np.maximum(np.abs(image[:, columns - 1] - image[:, columns - 2]),
                       np.abs(image[:, columns + 1] - image[:, columns]))
    strength = np.max(np.abs(middle), axis=2)
    outer_strength = np.max(outer, axis=2)
    correction = np.clip(middle, -6, 6) * 0.20
    mask = (strength < 20) & (outer_strength + 2 < strength * 0.55)
    correction *= mask[..., None]
    result[:, columns - 1] += correction
    result[:, columns] -= correction
    return result


def restore(source, prior, target):
    height, width = source.shape[:2]
    stock = source[..., :3].astype(np.float32)
    if np.all(stock == stock[0, 0]):
        return np.broadcast_to(source[0, 0, :3], (target[1], target[0], 3)).copy()

    source_guide = deblock_axis(deblock_axis(stock).transpose(1, 0, 2)).transpose(1, 0, 2)
    raster = cv2.resize(source_guide, target, interpolation=cv2.INTER_CUBIC)
    span_kernel = np.ones((3, 3), dtype=np.uint8)
    low = cv2.resize(cv2.erode(source_guide, span_kernel), target, interpolation=cv2.INTER_NEAREST)
    high = cv2.resize(cv2.dilate(source_guide, span_kernel), target, interpolation=cv2.INTER_NEAREST)
    blur_radius = 0.38 * (target[0] / width)
    profile = raster - cv2.GaussianBlur(raster, (0, 0), blur_radius)
    raster += np.clip(profile * 0.62, -7, 7)

    if prior is not None:
        # Carry only fine texture from the earlier source-derived restoration. Discard
        # its lower-frequency colour, silhouettes, lettering and edge placement.
        candidate = cv2.resize(prior[..., :3].astype(np.float32), target, interpolation=cv2.INTER_CUBIC)
        candidate -= cv2.GaussianBlur(candidate, (0, 0), blur_radius)
        neutral = np.clip((high - low).mean(axis=2) / 20, 0, 1)[..., None]
        raster += np.clip(candidate, -5, 5) * neutral * 0.40

    raster = np.clip(raster, low - 0.5, high + 0.5)
    # Match source-cell colours after optical filtering, without inserting the
    # hard source grid. This keeps stains, graphics, and atlas borders registered.
    correction = source_guide - cv2.resize(raster, (width, height), interpolation=cv2.INTER_AREA)
    raster += cv2.resize(correction, target, interpolation=cv2.INTER_LINEAR)
    return np.rint(np.clip(raster, 0, 255)).astype(np.uint8)


def linear_luma(rgb):
    pixels = rgb.astype(np.float32) / 255.0
    linear = np.where(pixels <= 0.04045, pixels / 12.92, ((pixels + 0.055) / 1.055) ** 2.4)
    return linear @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def evidence(source, rgb, alpha):
    h, w = source.shape[:2]
    authored_rgb = source[..., :3].astype(np.float32)
    reduction = cv2.resize(rgb.astype(np.float32), (w, h), interpolation=cv2.INTER_AREA)
    error = np.abs(reduction - authored_rgb)
    return {
        "source_size": [w, h],
        "size": [rgb.shape[1], rgb.shape[0]],
        "roundtrip_rgb_mae_255": round(float(error.mean()), 4),
        "roundtrip_rgb_p99_255": round(float(np.percentile(error, 99)), 4),
        "mean_linear_luma_change": round(float(linear_luma(rgb).mean() - linear_luma(authored_rgb).mean()), 7),
        "alpha_identical_to_authored_coverage": bool(np.array_equal(
            alpha, np.repeat(np.repeat(source[..., 3], rgb.shape[0] // h, axis=0), rgb.shape[1] // w, axis=1))),
    }


def build(source, prior_path, out):
    earlier = read(prior_path)
    out.mkdir(parents=True, exist_ok=True)
    manifest, qa = {}, {}
    written = {}
    for name in earlier:
        if Path(name).name != name:
            raise ValueError(f"Unexpected source path: {name}")
        original = open_rgba(source / name)
        h, w = original.shape[:2]
        factor = min(16, max(4, 2048 // max(h, w)))
        size = w * factor, h * factor
        # A byte identity, not a similar name, is required to share one output.
        digest = hashlib.sha256(original.tobytes()).hexdigest(), original.shape
        if digest not in written:
            prior = prior_path.parent / earlier[name]
            input_detail = open_rgba(prior) if prior.is_file() else None
            rgb = restore(original, input_detail, size)
            alpha = np.repeat(np.repeat(original[..., 3], factor, axis=0), factor, axis=1)
            Image.fromarray(np.dstack((rgb, alpha))).save(out / name, compress_level=3)
            stats = evidence(original, rgb, alpha)
            if stats["roundtrip_rgb_mae_255"] > 3.2 or not stats["alpha_identical_to_authored_coverage"]:
                raise ValueError(f"Original surface changed too far: {name}, {stats}")
            written[digest] = name, stats
        manifest[name], qa[name] = written[digest]
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out / "source-validation.json").write_text(json.dumps(qa, indent=2) + "\n")
    print(f"Restored {len(manifest)} source bindings from {len(written)} original colour images")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("prior_source_derived_manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    dest, original = args.output.resolve(), args.source.resolve()
    if dest == original or original in dest.parents:
        parser.error("stage artwork outside the original map pack")
    build(original, args.prior_source_derived_manifest.resolve(), dest)


if __name__ == "__main__":
    main()
