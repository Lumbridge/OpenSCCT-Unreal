"""Build exact 4x Enhanced Mount Hospital colour textures from the extracted PNGs.

Use the official https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/
realesr-general-x4v3.pth weights. CPU PyTorch, NumPy and Pillow are required.
The output directory is staging only. The source pack is opened read-only.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageFilter


SCALE = 4
WEIGHTS_SHA256 = "8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292"
COLOR_ROLES = frozenset(("base colour", "Self-illumination image", "Environment reflection image"))
LIGHT_ROLES = frozenset((
    "Self-illumination image", "Self-illumination mask", "Environment reflection image",
    "Specular reflection mask", "Particle or animated effect input", "Light or corona input",
))
GRAPHIC = re.compile(
    r"numlvl|psy|pan_|enseigne|door|box|crate|palette|graffiti|transition|wall_lvl|wallfloor|"
    r"building|sky|lune|envmap|glow|neon|machine|sgameplay", re.IGNORECASE
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Restorer:
    def __init__(self, weights, threads):
        import torch
        from torch import nn

        if sha256(weights) != WEIGHTS_SHA256:
            raise ValueError("Expected the official Real-ESRGAN general x4v3 v0.2.5.0 weights")
        torch.set_num_threads(threads)
        torch.set_num_interop_threads(1)
        # Compact SRVGG topology of the official general x4v3 checkpoint.
        layers = [nn.Conv2d(3, 64, 3, padding=1), nn.PReLU(64)]
        for _ in range(32):
            layers.extend((nn.Conv2d(64, 64, 3, padding=1), nn.PReLU(64)))
        layers.append(nn.Conv2d(64, 3 * SCALE ** 2, 3, padding=1))
        network = nn.Sequential(*layers)
        checkpoint = torch.load(weights, map_location="cpu", weights_only=True)
        parameters = checkpoint.get("params_ema", checkpoint.get("params"))
        network.load_state_dict({key.removeprefix("body."): value for key, value in parameters.items()})
        self.network = network.eval().to(memory_format=torch.channels_last)
        self.torch = torch

    def enlarge(self, image):
        torch = self.torch
        # The CNN has 34 convolutions with radius one. Wrap enough input on all
        # sides so every calculated output pixel sees the actual repeating neighbours.
        pad = 34
        src = np.asarray(image, dtype=np.float32) / 255.0
        wrapped = np.pad(src, ((pad, pad), (pad, pad), (0, 0)), mode="wrap")
        tensor = torch.from_numpy(wrapped).permute(2, 0, 1).unsqueeze(0).contiguous(memory_format=torch.channels_last)
        with torch.inference_mode():
            residual = torch.nn.functional.pixel_shuffle(self.network(tensor), SCALE)
            enlarged = residual + torch.nn.functional.interpolate(tensor, scale_factor=SCALE, mode="nearest")
        offset = pad * SCALE
        cropped = enlarged[0, :, offset:offset + image.height * SCALE, offset:offset + image.width * SCALE]
        return cropped.permute(1, 2, 0).contiguous().numpy() * 255.0


def area_sample(values, width, height):
    return values.reshape(height, SCALE, width, SCALE, 3).mean(axis=(1, 3))


def refine(rgb, restorer):
    width, height = rgb.size
    size = (width * SCALE, height * SCALE)
    expanded = rgb.resize(size, Image.Resampling.BICUBIC)
    reference = np.asarray(expanded, dtype=np.float32)
    original = np.asarray(rgb, dtype=np.float32)
    if restorer is not None:
        detail = restorer.enlarge(rgb) - reference
        candidate = reference + 0.85 * np.clip(detail, -36.0, 36.0)
    else:
        # Restore local edge contrast without synthesising glyphs, emblems or panel lines.
        sharp = expanded.filter(ImageFilter.UnsharpMask(radius=1.3, percent=65, threshold=3))
        candidate = np.asarray(sharp, dtype=np.float32)

    # Reconcile the 4x output with original low-frequency colour. Apply a
    # smooth, channel-wise correction so no source-resolution grid is drawn.
    delta = original - area_sample(candidate, width, height)
    for channel in range(3):
        correction = Image.fromarray(delta[:, :, channel]).resize(size, Image.Resampling.BILINEAR)
        candidate[:, :, channel] += np.asarray(correction)
    return np.rint(np.clip(candidate, 0.0, 255.0)).astype(np.uint8)


def write_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def build(source, inventory, output, weights, threads):
    audit = json.loads(inventory.read_text(encoding="utf-8-sig"))
    if audit.get("map") != "MounE":
        raise ValueError("Expected the Enhanced Mount Hospital texture inventory")
    if source.resolve() == output.resolve() or source.resolve() in output.resolve().parents:
        raise ValueError("Output must be outside the original texture directory")
    output.mkdir(parents=True, exist_ok=True)
    aliases = defaultdict(list)
    roles_by_name = {}
    for name, row in sorted(audit["textures"].items()):
        roles = frozenset(use["role"] for use in row["uses"])
        roles_by_name[name] = roles
        if roles & COLOR_ROLES:
            if Path(name).name != name or Path(name).suffix.lower() != ".png":
                raise ValueError("Expected an original PNG basename: " + name)
            aliases[row["sha256"]].append(name)

    restorer = Restorer(weights, threads) if weights else None
    manifest = {}
    diagnostics = {}
    for source_hash, names in aliases.items():
        for name in names:
            if sha256(source / name) != source_hash:
                raise ValueError("Source texture differs from the audit: " + name)
        conservative = any(GRAPHIC.search(name) or roles_by_name[name] & LIGHT_ROLES for name in names)
        with Image.open(source / names[0]) as image:
            rgb = image.convert("RGB")
            alpha = image.convert("RGBA").getchannel("A")
            neural = restorer if not conservative and min(rgb.size) >= 32 else None
            method = "Real-ESRGAN general x4v3, constrained" if neural else "bicubic and local edge restoration"
            pixels = refine(rgb, neural)
            width, height = rgb.size
            delta = area_sample(pixels.astype(np.float32), width, height) - np.asarray(rgb, dtype=np.float32)
            upscaled = Image.fromarray(pixels)
            if alpha.getextrema() != (255, 255):
                upscaled.putalpha(alpha.resize(upscaled.size, Image.Resampling.NEAREST))
            path = output / names[0]
            temporary = path.with_suffix(".tmp")
            upscaled.save(temporary, format="PNG", compress_level=3)
            temporary.replace(path)
            for name in names:
                manifest[name] = path.name
                diagnostics[name] = {
                    "source_sha256": source_hash, "source_size": list(rgb.size), "output_size": list(upscaled.size),
                    "roles": sorted(roles_by_name[name]), "method": method,
                    "authored_alpha": alpha.getextrema() != (255, 255),
                    "area_downsample_rgb_mae": round(float(np.abs(delta).mean()), 3),
                    "area_downsample_rgb_bias": [round(float(v), 3) for v in delta.mean(axis=(0, 1))],
                }
            # Supply a valid, incremental batch for importer QA during a long CPU run.
            write_json(output / "manifest.json", dict(sorted(manifest.items())))
            print(f"{len(manifest):3d}  {', '.join(names)}  {rgb.size} -> {upscaled.size}  {method}", flush=True)

    write_json(output / "source-report.json", {
        "map": "MounE", "scale": SCALE, "weights_sha256": sha256(weights) if weights else None,
        "textures": dict(sorted(diagnostics.items())),
        "other_roles": {name: sorted(roles) for name, roles in roles_by_name.items() if name not in manifest},
    })


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="the original MounE/render/textures directory")
    parser.add_argument("inventory", type=Path, help="Mount Hospital texture-use audit")
    parser.add_argument("output", type=Path, help="staging directory for the PNGs and manifest")
    parser.add_argument("--weights", type=Path, help="official general x4v3 .pth checkpoint")
    parser.add_argument("--threads", type=int, default=3, help="CPU inference threads")
    args = parser.parse_args()
    build(args.source, args.inventory, args.output, args.weights, max(1, args.threads))
