"""Restore the paper-label face of the Mount Hospital Box1 atlas."""

import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


FACE = (1, 172, 65, 254)
PAPER = np.float32([[14.7, 200.7], [34.4, 194.65], [37.95, 205.95], [17.65, 212.35]])
GENERATED_FACE = (0, 834, 325, 1254)
GENERATED_PAPER = np.float32([[73.1, 983.8], [168.3, 955.1], [185.8, 1008.8], [84.4, 1038.2]])
WORK_SIZE = 2048


def spread(array, sigma):
    return cv2.GaussianBlur(array, (0, 0), sigma)


def soften_border(width, height, ramp):
    rows, cols = np.ogrid[:height, :width]
    available = np.minimum(np.minimum(rows, height - 1 - rows), np.minimum(cols, width - 1 - cols))
    return np.minimum(available / ramp, 1.0).astype(np.float32)


def fit(values, source):
    return values + (source.mean(axis=(0, 1)) - values.mean(axis=(0, 1)))


def project(source, corners, strength):
    height, width = source.shape[:2]
    transfer = cv2.getPerspectiveTransform(
        np.float32([[0, 0], [width - 1, 0], [width - 1, height - 1], [0, height - 1]]), corners
    )
    return cv2.warpPerspective(source, transfer, (WORK_SIZE, WORK_SIZE), flags=strength)


def apply_grain(active, generated, scale):
    x0, y0, x1, y1 = [int(value * scale) for value in FACE]
    pixels = active[y0:y1, x0:x1]
    height, width = pixels.shape[:2]
    grain_source = np.asarray(generated.crop(GENERATED_FACE).resize((width, height), Image.Resampling.LANCZOS),
                              dtype=np.float32)
    luma = grain_source @ np.float32([.2126, .7152, .0722])
    fine = spread(luma, .58) - spread(luma, 4.8)
    coarse = spread(luma, 3.0) - spread(luma, 17.0)
    detail = np.clip(fine * 1.10 + coarse * .40, -13.0, 16.0)

    # Let the source's scrape, weathering, and colour set the face underneath the fibres.
    # The generated label, which has different print, only contributes outside the source paper.
    rows, cols = np.ogrid[y0:y1, x0:x1]
    label = ((cols / scale - 26.0) / 14.7) ** 2 + ((rows / scale - 203.0) / 12.2) ** 2
    body = np.clip((label - .79) / .52, 0.0, 1.0)
    border = soften_border(width, height, 2 * scale)
    mix = (border * body)[..., None]
    softened = spread(pixels, .38 * scale)
    pixels[:] = pixels + mix * ((softened - pixels) * .42 + detail[..., None])


def restore_label(original, target, generated, scale):
    width, height = 192, 100
    canonical = np.float32([[0, 0], [width - 1, 0], [width - 1, height - 1], [0, height - 1]])
    turn = cv2.getPerspectiveTransform(PAPER, canonical)
    face = cv2.warpPerspective(original, turn, (width, height), flags=cv2.INTER_CUBIC)
    face = np.clip(face, 0, 255)
    # Retain the original tiny print as ink shapes. The source has no recoverable character
    # outlines, so this sharpens the actual source marks without replacing them with new words.
    low = spread(face, 3.8)
    soft = spread(face, .85)
    sharper = np.clip(face + (soft - low) * 1.62, 0.0, 255.0)
    reference_transfer = cv2.getPerspectiveTransform(GENERATED_PAPER, canonical)
    paper = cv2.warpPerspective(np.asarray(generated, dtype=np.float32), reference_transfer, (width, height),
                                flags=cv2.INTER_CUBIC)
    weights = np.float32([.2126, .7152, .0722])
    paper_luma = paper @ weights
    authored_luma = face @ weights
    ink_signal = np.clip((spread(authored_luma, 8.0) - authored_luma + 8.0) / 31.0, 0.0, 1.0)
    grain = np.clip(spread(paper_luma, .52) - spread(paper_luma, 3.2), -19.0, 15.0)
    texture = grain * (.18 + .73 * ink_signal)
    restored = np.clip(fit(sharper + texture[..., None], face), 0.0, 255.0)
    coverage = np.ones((height, width), dtype=np.float32)
    for y in [0, height - 1]:
        for x in [0, width - 1]:
            cv2.circle(coverage, (x, y), 3, 0.0, thickness=-1, lineType=cv2.LINE_8)
    coverage = spread(coverage, .92)
    corners = PAPER * scale
    alpha = project(coverage, corners, cv2.INTER_LINEAR)[..., None]
    label = project(restored, corners, cv2.INTER_CUBIC)
    return target * (1.0 - alpha) + label * alpha


def restore(original_path, current_path, imagegen_path, output_path):
    original = np.asarray(Image.open(original_path).convert("RGB"), dtype=np.float32)
    base = Image.open(current_path).convert("RGBA")
    generated = Image.open(imagegen_path).convert("RGB")
    if original.shape != (256, 256, 3) or base.size != (WORK_SIZE, WORK_SIZE) or generated.size != (1254, 1254):
        raise ValueError("Expected the original 256 atlas, its live 2K replacement, and the registered ImageGen atlas")

    before = np.asarray(base).copy()
    restored = before[..., :3].astype(np.float32)
    scale = WORK_SIZE / original.shape[0]
    apply_grain(restored, generated, scale)
    restored = restore_label(original, restored, generated, scale)
    result = before.copy()
    result[..., :3] = np.clip(np.rint(restored), 0, 255).astype(np.uint8)
    changed = np.any(result != before, axis=2)
    rows, cols = np.nonzero(changed)
    if not len(rows) or cols.min() < 8 or cols.max() >= 520 or rows.min() < 1376 or rows.max() >= 2032:
        raise RuntimeError("The correction must stay inside the original labelled face")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(result, "RGBA").save(output_path)
    stats = {
        "original_atlas_dimensions": [256, 256],
        "restored_atlas_dimensions": [WORK_SIZE, WORK_SIZE],
        "changed_pixels": int(changed.sum()),
        "changed_bounds_2k": [int(cols.min()), int(rows.min()), int(cols.max()), int(rows.max())],
        "source_paper_vertices": PAPER.tolist(),
        "alpha_unchanged": bool(np.array_equal(result[..., 3], before[..., 3])),
    }
    print(json.dumps(stats))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ("original", "current", "imagegen", "output"):
        p.add_argument(arg, type=Path)
    restore(**{key + "_path": value for key, value in vars(p.parse_args()).items()})
