"""Combine source-based artwork and regenerate its matching glow masks."""

import argparse
from collections import defaultdict
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

from restore_hospital_original_colors import evidence, open_rgba, read


VENDING_ATLAS = Path(__file__).resolve().parents[2] / "Art/Maps/MounE/Prison_TEX_machine_HD.png"
VENDING_TEXTURES = {"Prison_TEX_machine.png", "Prison_TEX_machineEMF.png"}


def save_json(path, contents):
    path.write_text(json.dumps(contents, indent=2) + "\n", encoding="utf-8")


def make_colour_plan(source, prepared, external, output_directory):
    if not VENDING_ATLAS.is_file():
        raise FileNotFoundError(f"The HD Mount Hospital restoration requires the approved local vending cabinet atlas at {VENDING_ATLAS}")
    cabinet_bytes = VENDING_ATLAS.read_bytes()
    base = read(prepared / "manifest.json")
    replacements = {name: (prepared / file, "source-guided restoration") for name, file in base.items()}
    groups = defaultdict(list)
    for name, file in base.items():
        groups[file].append(name)
    for folder, label in (
        ("OriginalGraffiti", "original paint strokes and tags"),
        ("OriginalSignRestoration", "original typography, arrows and wear"),
        ("OriginalSewerSurfaces/BaseColor", "original masonry and hardware surfaces"),
        ("OriginalProps", "original prop artwork"),
        ("OriginalAtmosphere", "original atmosphere or backdrop artwork"),
    ):
        directory = external / folder
        for name, file in read(directory / "manifest.json").items():
            for alias in groups[base[name]]:
                replacements[alias] = (directory / file, label)
    sewer = external / "OriginalSewer"
    replacements["Prison_TEX_plaquegout.png"] = (sewer / "Prison_TEX_plaquegout.png", "original 84 slots and rim")
    replacements["Prison_TEX_water.png"] = (sewer / "Prison_TEX_water.png", "original water ripple pattern")

    audit, paths = {}, {}
    for name, (path, description) in replacements.items():
        original = open_rgba(source / name)
        output = open_rgba(path)
        h, w = original.shape[:2]
        height, width = output.shape[:2]
        if width % w or height % h or width // w != height // h or width < 2 * w:
            raise ValueError(f"Changed UV aspect or unexpected size for {name}")
        alpha = output[..., 3]
        stats = evidence(original, output[..., :3], alpha)
        if not stats["alpha_identical_to_authored_coverage"]:
            raise ValueError(f"Original UV mask changed for {name}")
        if name in VENDING_TEXTURES:
            # Both slots must keep the complete painted cabinet, including its new labels.
            if path.read_bytes() != cabinet_bytes:
                raise ValueError(f"Stale vending artwork for {name}. Regenerate the original props stage.")
            description = "full HD vending cabinet, shelves and controls"
        elif stats["roundtrip_rgb_mae_255"] > 4.0:
            # Recover the original local colour level on very coarse surfaces.
            # Corrections are sheet-sized and do not replace the restored fine edges.
            rgb = output[..., :3].astype(np.float32)
            low = cv2.resize(rgb, (w, h), interpolation=cv2.INTER_AREA)
            correction = cv2.resize(original[..., :3].astype(np.float32) - low, (width, height),
                                    interpolation=cv2.INTER_LINEAR)
            adjusted = np.rint(np.clip(rgb + correction, 0, 255)).astype(np.uint8)
            toned = output_directory / "SourceColourFit"
            toned.mkdir(exist_ok=True)
            path = toned / name
            Image.fromarray(np.dstack((adjusted, alpha))).save(path, compress_level=3)
            stats = evidence(original, adjusted, alpha)
            if stats["roundtrip_rgb_mae_255"] > 4.0:
                raise ValueError(f"{name} is too different from its source: {stats}")
        paths[name] = str(path.resolve(strict=True))
        audit[name] = dict(stats, artwork=description, source_sha256=hashlib.sha256((source / name).read_bytes()).hexdigest())
    return paths, audit


def make_masks(source, mask_plan, manifest, output):
    plan = deepcopy(mask_plan)
    plan["images"] = {}
    output.mkdir(parents=True, exist_ok=True)
    for row in plan["bindings"].values():
        file = row["file"]
        old_name = row["stock"].rsplit(".", 1)[-1] + ".png"
        glow = row["glow"].split("/")[-1].split(".")[0]
        colour_name = glow.removeprefix("T_MounE_").removesuffix("_Base") + ".png"
        with Image.open(manifest[colour_name]) as colour, Image.open(source / old_name) as reference:
            stock = reference.convert("RGBA")
            alpha = stock.getchannel("A").resize(colour.size, Image.Resampling.BILINEAR)
            bits = Image.new("RGBA", colour.size, (0, 0, 0, 0))
            bits.putalpha(alpha)
            bits.save(output / file, compress_level=3)
            plan["images"][file] = {
                "original_size": list(stock.size),
                "size": list(colour.size),
                "source_alpha_mean": float(np.asarray(stock.getchannel("A")).mean()),
                "restored_alpha_mean": float(np.asarray(alpha).mean()),
            }
        row.pop("glow_integral_change_percent", None)
    save_json(output / "self-illum-masks.json", plan)


def build(source, prepared, specialists, mask_plan, output):
    output.mkdir(parents=True, exist_ok=True)
    paths, audit = make_colour_plan(source, prepared, specialists, output)
    make_masks(source, mask_plan, paths, output / "GlowMasks")
    save_json(output / "restoration-audit.json", audit)
    relative = {name: Path(os.path.relpath(path, output)).as_posix() for name, path in paths.items()}
    save_json(output / "manifest.json", relative)
    print(f"Validated {len(audit)} original colour inputs and six restored glow masks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("prepared", type=Path)
    parser.add_argument("specialists", type=Path)
    parser.add_argument("old_mask_json", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.source, args.prepared, args.specialists, read(args.old_mask_json), args.output)


if __name__ == "__main__":
    main()
