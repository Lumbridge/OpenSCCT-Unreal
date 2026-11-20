"""Restore the original Mount Hospital sewer artwork; input PNGs and prior neural restores are read-only."""
import hashlib
import json

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter
from scipy.ndimage import maximum_filter, minimum_filter


LUMA = np.array([.2126, .7152, .0722], dtype=np.float32)
ASSETS = (
    "WAL_brick07b", "WAL_brick01b", "WAL_cretedirty01", "FLO_concrete02b",
    "pillon1", "wallegout04", "WAL_wall08", "ACIER", "trouegouts",
)


def smoothstep(t):
    q = np.clip(t, 0., 1.)
    return q * q * (3. - 2. * q)


def restored_block_edges(image):
    """Damp only supported discontinuities on the source's four-texel compression lattice."""
    current = image.copy()
    for axis in (0, 1):
        line = np.moveaxis(image, axis, 0)
        correction = np.moveaxis(current, axis, 0)
        positions = np.arange(4, len(line) - 2, 4)
        left = line[positions - 1]
        right = line[positions]
        edge = right - left
        left_step = left - line[positions - 2]
        right_step = line[positions + 1] - right
        expected = (left_step + right_step) * .5
        strength = np.abs(edge @ LUMA)
        excess = strength - np.maximum(np.abs(left_step @ LUMA), np.abs(right_step @ LUMA))
        # A genuine high-contrast seam, mortar edge, or rust pit is protected.
        weight = smoothstep((excess - 2.) / 7.) * (1. - smoothstep((strength - 13.) / 15.))
        delta = np.clip(.22 * (edge - expected), -2.6, 2.6) * weight[..., None]
        correction[positions - 1] += delta
        correction[positions] -= delta
    return np.clip(current, 0, 255)


def enlarge(image, factor, pure=False):
    pad = 5
    image = np.pad(image, ((pad, pad), (pad, pad), (0, 0)), mode="edge" if pure else "wrap")
    size = (image.shape[1] * factor, image.shape[0] * factor)
    source_img = Image.fromarray(np.uint8(image.round()))
    pixels = np.asarray(source_img.resize(size, Image.Resampling.LANCZOS), dtype=np.float32)
    return pixels[pad*factor:-pad*factor, pad*factor:-pad*factor].copy()


def rgb_linear(c):
    c = np.clip(c / 255., 0., 1.)
    return np.where(c < .04045, c / 12.92, ((c + .055) / 1.055)**2.4)


def rgb_srgb(c):
    c = np.clip(c, 0., 1.)
    return 255 * np.where(c < .0031308, c * 12.92, 1.055 * c**(1/2.4) - .055)


def levels(value, original):
    orig_l = rgb_linear(original).mean(axis=(0, 1), dtype=np.float64)
    target_l = rgb_linear(value)
    correction = orig_l / np.maximum(target_l.mean(axis=(0, 1), dtype=np.float64), 1e-6)
    return rgb_srgb(target_l * correction.astype(np.float32))


def restore(src, source_guidance, packed):
    initial = restored_block_edges(src)
    base = enlarge(initial, 8, pure=packed)
    if source_guidance is not None:
        # Only take restrained fine structure from the independently restored same artwork.
        plain_four = enlarge(src, 4, pure=packed)
        learned = source_guidance - plain_four
        learned -= gaussian_filter(learned, (3.2, 3.2, 0.), mode="nearest" if packed else "wrap")
        # Treat residuals as signed centered channels to avoid biasing resize quantization.
        learned_at_target = np.stack([
            np.asarray(Image.fromarray(learned[..., i]).resize(
                (base.shape[1], base.shape[0]), Image.Resampling.BICUBIC), dtype=np.float32)
            for i in range(3)
        ], axis=-1)
        base += .42 * np.clip(learned_at_target, -11., 11.)
    # Remove ringing overshoot with the source neighbourhood's extents.
    floor = np.repeat(np.repeat(minimum_filter(src, (3, 3, 1), mode="nearest" if packed else "wrap"), 8, axis=0), 8, axis=1)
    ceil = np.repeat(np.repeat(maximum_filter(src, (3, 3, 1), mode="nearest" if packed else "wrap"), 8, axis=0), 8, axis=1)
    return np.clip(base, floor - 1.0, ceil + 1.0)


def per_source_texel(image, src):
    return np.asarray(image, dtype=np.float32).reshape(src.shape[0], 8, src.shape[1], 8, 3).mean(axis=(1, 3))


def gradient_correlation(a, b):
    a = a @ LUMA
    b = b @ LUMA
    x = np.concatenate((np.diff(a, axis=0).ravel(), np.diff(a, axis=1).ravel()))
    y = np.concatenate((np.diff(b, axis=0).ravel(), np.diff(b, axis=1).ravel()))
    return float(np.corrcoef(x, y)[0, 1])


def global_luma(image):
    return float(rgb_linear(image).mean(axis=(0, 1), dtype=np.float64) @ LUMA)


def staged_guidance(filename, src, prior, report, digest):
    path = prior / filename
    if not path.is_file():
        return None
    descriptor = report.get("textures", {}).get(filename, {})
    if descriptor.get("source_sha256") != digest:
        return None
    with Image.open(path) as texture:
        if texture.size != (4 * src.shape[1], 4 * src.shape[0]):
            return None
        return np.asarray(texture.convert("RGB"), dtype=np.float32)


def stage(key, source, prior, report, grille_path, base_color):
    filename = f"Prison_TEX_{key}.png"
    path = source / filename
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    with Image.open(path) as decoded:
        alpha = decoded.convert("RGBA").getchannel("A")
        src = np.asarray(decoded.convert("RGB"), dtype=np.float32)
    guidance = staged_guidance(filename, src, prior, report, digest)
    is_atlas = key == "trouegouts"
    artwork = restore(src, guidance, is_atlas)
    w = artwork.shape[1]
    if is_atlas:
        with Image.open(grille_path) as restored:
            if restored.size != (w, artwork.shape[0]):
                raise ValueError("Restored grille must be at final exact size")
            grille = np.asarray(restored.convert("RGB"), dtype=np.float32)
        # Preserve the source artwork's rim, seam and right face exactly as restored by its owner.
        # Merge only the left beam/tunnel half, with a short UV-aligned transition before midpoint.
        fade = (1. - smoothstep(((np.arange(w // 2) + .5) / w - .465) / .035))[None, :, None]
        left = levels(artwork[:, :w//2], src[:, :src.shape[1]//2])
        artwork = grille.copy()
        artwork[:, :w//2] += fade * (left - artwork[:, :w//2])
    else:
        artwork = levels(artwork, src)
    image_rgb = np.clip(np.rint(artwork), 0, 255).astype(np.uint8)
    merged_right = bool(np.array_equal(image_rgb[:, w//2:], grille[:, w//2:])) if is_atlas else None
    assert not is_atlas or merged_right
    alpha_codes = alpha.resize((w, artwork.shape[0]), Image.Resampling.NEAREST)
    png = Image.fromarray(image_rgb)
    if alpha.getextrema() != (255, 255):
        png.putalpha(alpha_codes)
    png.save(base_color / filename, compress_level=3)
    mip = per_source_texel(image_rgb, src)
    mae = float(np.abs(mip - src).mean())
    correlation = gradient_correlation(src, mip)
    error = 100 * (global_luma(image_rgb.astype(np.float32)) / global_luma(src) - 1)
    if mae > 5. or correlation < .90 or abs(error) > .9:
        raise ValueError(f"{filename}: register or luma mismatch: {mae=:.4f} {correlation=:.4f} {error=:.4f}")
    before = Image.fromarray(src.astype(np.uint8)).resize((w, artwork.shape[0]), Image.Resampling.NEAREST)
    method = report["textures"].get(filename, {}).get("method", "")
    return {"texture": filename, "size": png.size, "source_sha256": digest,
            "original_8x": True, "same_art_neural_detail": guidance is not None and method.startswith("Real-ESRGAN"),
            "source_area_mae": round(mae, 3), "source_gradient_correlation": round(correlation, 4),
            "relative_linear_mean_difference_pct": round(error, 4),
            "original_near_black_fraction": round(float(((src @ LUMA) < 20).mean()), 5),
            "restored_mip_near_black_fraction": round(float(((mip @ LUMA) < 20).mean()), 5),
            "original_grille_right_byte_exact": merged_right, "source_alpha_preserved": True}, before, png.convert("RGB")


def run(source, output, prior, grille_path):
    base_color = output / "BaseColor"
    base_color.mkdir(parents=True, exist_ok=True)
    source_report = json.loads((prior / "source-report.json").read_text(encoding="utf-8"))
    validated = {}
    images = []
    for name in ASSETS:
        m, before, after = stage(name, source, prior, source_report, grille_path, base_color)
        validated[m["texture"]] = m
        images.append((m["texture"], before, after))
        print(json.dumps(m), flush=True)
    (output / "AssetQA.json").write_text(json.dumps(validated, indent=2) + "\n", encoding="utf-8")
    artifacts = {filename: filename for filename in validated}
    (base_color / "manifest.json").write_text(json.dumps(artifacts, indent=2) + "\n", encoding="utf-8")
    sheet = Image.new("RGB", (994, 32 + len(images) * 272), (25, 27, 33))
    labels = ImageDraw.Draw(sheet)
    labels.text((12, 11), "Original PNG (nearest magnification)", fill="white")
    labels.text((510, 11), "Same artwork, restored", fill="white")
    for idx, (name, before, after) in enumerate(images):
        y = 32 + idx * 272
        labels.text((12, y), name, fill="white")
        for x, image in ((12, before), (510, after)):
            cw = min(image.width, max(720, int(.42 * image.width)))
            ch = min(image.height, 550)
            left = (image.width - cw) // 2
            top = (image.height - ch) // 2
            chosen = image.crop((left, top, left+cw, top+ch))
            chosen.thumbnail((470, 247), Image.Resampling.NEAREST)
            sheet.paste(chosen, (x, y + 18))
    sheet.save(output / "SourceVersusRestored.jpg", quality=92)
