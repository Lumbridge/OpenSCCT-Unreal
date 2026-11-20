"""Resample Mount Hospital's self-illumination alpha without changing its overall coverage."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


MAP = "/Game/Imported/Maps/MounE"


def load_image(inventory, textures, path):
    exported = textures[path]["image"]
    if not exported:
        raise ValueError(f"No source image was exported for {path}")
    return Image.open(inventory.parent / exported).convert("RGBA")


def quantize_alpha(resampled, total):
    alpha = np.rint(resampled).astype(np.int16)
    remainder = total - alpha.sum(dtype=np.int64)
    if remainder:
        # Spend the rounding difference at samples rounded farthest in the wrong direction.
        # Do not add alpha to true zeros or remove it from fully opaque samples.
        direction = int(np.sign(remainder))
        eligible = (resampled > 0.0) & (resampled < 255.0) & (alpha + direction >= 0) & (alpha + direction <= 255)
        candidates = np.flatnonzero(eligible)
        cost = ((alpha - resampled) * direction).ravel()[candidates]
        count = abs(remainder)
        if count > len(candidates):
            raise ValueError("Could not hold the source mask coverage")
        chosen = candidates[np.argpartition(cost, count - 1)[:count]]
        alpha.ravel()[chosen] += direction
    if alpha.sum(dtype=np.int64) != total:
        raise ValueError("The output alpha changed the source mask coverage")
    return alpha.astype(np.uint8)


def boundary_steps(alpha, scale):
    values = alpha.astype(np.float32)
    rows = np.abs(np.diff(values, axis=0))[scale - 1::scale, :]
    columns = np.abs(np.diff(values, axis=1))[:, scale - 1::scale]
    return float((rows.mean() + columns.mean()) * .5)


def build(source, size):
    alpha = np.asarray(source.getchannel("A"))
    scale, remainder = divmod(size[0], source.width)
    if remainder or scale < 2 or size[1] != source.height * scale:
        raise ValueError(f"Expected a larger mask at an integer scale, got {source.size} -> {size}")
    if alpha.min() == alpha.max():
        raise ValueError("Expected an authored self-illumination silhouette")
    # Sample the continuous function the stock mask receives under bilinear filtering.
    filtered = np.asarray(Image.fromarray(alpha.astype(np.float32)).resize(size, Image.Resampling.BILINEAR))
    coverage = int(alpha.sum(dtype=np.int64)) * scale * scale
    result = quantize_alpha(filtered, coverage)
    nearest = alpha.repeat(scale, axis=0).repeat(scale, axis=1)
    metrics = {
        "original_size": list(source.size),
        "size": list(size),
        "mean_alpha_code": float(alpha.mean()),
        "mean_alpha_code_change": float(result.mean() - alpha.mean()),
        "source_boundary_steps_nearest": boundary_steps(nearest, scale),
        "source_boundary_steps_filtered": boundary_steps(result, scale),
    }
    return result, filtered, metrics


def glow_difference(image, stock_filter, mask, srgb):
    rgb = np.asarray(image, dtype=np.float32)[..., :3] / 255.0
    if srgb:
        rgb = np.where(rgb <= 0.04045, rgb / 12.92, np.power((rgb + .055) / 1.055, 2.4))
    luma = rgb @ (0.2126, 0.7152, 0.0722)
    reference = np.mean(luma * stock_filter)
    return float(100.0 * (np.mean(luma * mask) / reference - 1.0)) if reference > 0 else 0.0


def prepare(inventory, output):
    source = json.loads(inventory.read_text(encoding="utf-8"))
    if source["map"] != MAP + "/MounE":
        raise ValueError("Expected the exported Enhanced Mount Hospital inventory")
    output.mkdir(parents=True, exist_ok=True)
    textures = source["textures"]
    images = {}
    bindings = {}
    for material, record in source["materials"].items():
        parameters = record["bindings"]
        mask = parameters.get("SelfIllumMask", {}).get("texture", "")
        if not mask.startswith((MAP + "/Extra/", MAP + "/world/Textures/")):
            continue
        glow = parameters["SelfIllumTexture"]["texture"]
        lit_image = load_image(inventory, textures, glow)
        if mask not in images:
            stock = load_image(inventory, textures, mask)
            alpha, baseline, metrics = build(stock, lit_image.size)
            file = Path(mask.split(".", 1)[0]).name + "_SelfIllumMask.png"
            image = Image.new("RGBA", lit_image.size, (255, 255, 255, 255))
            image.putalpha(Image.fromarray(alpha))
            image.save(output / file)
            images[mask] = (file, alpha, baseline, metrics)
        file, alpha, baseline, _ = images[mask]
        error = glow_difference(lit_image, baseline, alpha, textures[glow]["srgb"])
        if abs(error) > 0.25:
            raise ValueError(f"{material}: integrated glow changed by {error:.2f}%")
        bindings[material] = {"stock": mask, "glow": glow, "file": file, "glow_integral_change_percent": error}
    if len(bindings) != 7 or len(images) != 6:
        raise ValueError(f"Expected 7 live mask bindings and 6 source textures, got {len(bindings)} and {len(images)}")
    plan = {"map": MAP, "bindings": bindings, "images": {file: metrics for file, _, _, metrics in images.values()}}
    manifest = output / "self-illum-masks.json"
    temporary = manifest.with_suffix(".tmp")
    temporary.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    temporary.replace(manifest)
    for file, metrics in plan["images"].items():
        print(f"{file}: {metrics['original_size']} -> {metrics['size']}, "
              f"alpha change {metrics['mean_alpha_code_change']:+.3f}, "
              f"boundary step {metrics['source_boundary_steps_nearest']:.2f} -> {metrics['source_boundary_steps_filtered']:.2f}")
    print(f"Prepared {len(images)} masks for {len(bindings)} materials. "
          f"Largest modeled brightness drift {max(abs(row['glow_integral_change_percent']) for row in bindings.values()):.3f}%.")


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("inventory", type=Path)
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    prepare(args.inventory, args.output)
