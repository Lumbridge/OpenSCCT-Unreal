"""Prepare or validate the inputs for the private Enhanced Mount Hospital level."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from restore_hospital_original_colors import read
from restore_hospital_textures import INVENTORY, validate_source
from stage_hospital_restoration import VENDING_ATLAS
from upscale_hospital_base_colors import sha256


TOOLS = Path(__file__).resolve().parent
SHARED_COLOURS = TOOLS / "hospital-shared-colours.json"
ACTIVE_SKINS = TOOLS / "hospital-active-skins.json"
GEOMETRY_INPUTS = ("world.glb", "lighting.json", "lightbeams.json", "materials.json")


def python_script(name, *arguments):
    subprocess.run([sys.executable, "-B", str(TOOLS / name), *(str(value) for value in arguments)], check=True)


def stock_files(content, render):
    inventory = read(INVENTORY)
    validate_source(render, inventory)
    files = [render / name for name in GEOMETRY_INPUTS]
    files.extend(render / "textures" / name for name in inventory["textures"])
    for inventory_path, folder in (
        (SHARED_COLOURS, "shared/fx/meshes/textures"),
        (ACTIVE_SKINS, "shared/render/gameplay/textures"),
    ):
        for name, row in read(inventory_path)["textures"].items():
            original = content / folder / name
            if sha256(original) != row["sha256"]:
                raise ValueError(f"The original shared-prop image differs from the audited input: {original}")
            files.append(original)
    return {path.relative_to(content).as_posix(): sha256(path) for path in files}


def active_files(stage):
    """Follow only the manifests consumed by the next editor pass."""
    colours = stage / "Colours/base-color-plan.json"
    masks = stage / "Textures/OriginalRestoration/GlowMasks/self-illum-masks.json"
    lightmaps = stage / "Lightmaps/lightmap-manifest.json"
    shaft = stage / "SideStairs/SideStairsShaft.json"
    shared = stage / "SharedColours/manifest.json"
    skins = stage / "ActiveSkins/manifest.json"
    drain = stage / "Textures/Specialists/OriginalSewer/Prison_TEX_plaquegout_N.png"
    colour_data = read(colours)
    mask_data = read(masks)
    lightmap_data = read(lightmaps)
    shaft_data = read(shaft)
    shared_data = read(shared)
    skin_data = read(skins)
    originals = read(INVENTORY)["glow_masks"]
    if (colour_data["map"] != "MounE" or len(colour_data["textures"]) != 92
            or mask_data["map"] != originals["map"] or mask_data["bindings"] != originals["bindings"]
            or len(mask_data["images"]) != 6 or lightmap_data["map"] != "MounE"
            or len(lightmap_data["pages"]) != 3 or shaft_data["map"] != "MounE"
            or shaft_data["light"] != "Light3517"
            or shared_data.keys() != read(SHARED_COLOURS)["textures"].keys()
            or skin_data.keys() != read(ACTIVE_SKINS)["textures"].keys()):
        raise ValueError("Unexpected Mount Hospital preview slots, atlas pages, or Side Stairs fixture")

    files = [colours, masks, lightmaps, shaft, shared, skins, drain]
    files.extend(colours.parent / item["file"] for item in colour_data["textures"].values())
    files.extend(masks.parent / name for name in mask_data["images"])
    files.extend(lightmaps.parent / item["file"] for item in lightmap_data["pages"].values())
    files.extend(shaft.parent / shaft_data[name] for name in ("mesh", "transmission", "cast_depth"))
    files.extend(shared.parent / name for name in shared_data.values())
    files.extend(skins.parent / name for name in skin_data.values())
    return {path.resolve(strict=True).relative_to(stage).as_posix(): sha256(path) for path in files}


def prepare(content, render, stage, args, originals):
    if stage.exists() and (not stage.is_dir() or any(stage.iterdir())):
        raise ValueError(f"Use an empty stage for initial preparation: {stage}")
    arguments = [render.parent, stage / "Textures", "--threads", args.threads]
    if args.weights is not None:
        arguments.extend(("--weights", args.weights.resolve(strict=True)))
    if args.neural_python is not None:
        arguments.extend(("--neural-python", args.neural_python))
    python_script("restore_hospital_textures.py", *arguments)
    python_script("prepare_hospital_base_colors.py", stage / "Textures/OriginalRestoration/manifest.json",
                  INVENTORY, render / "textures", stage / "Colours")
    python_script("prepare_hospital_lightmaps.py", render, stage / "Lightmaps")
    python_script("prepare_hospital_side_stairs_beam.py", render, stage / "SideStairs")
    python_script("upscale_hospital_base_colors.py", content / "shared/fx/meshes/textures", SHARED_COLOURS,
                  stage / "SharedColours")
    python_script("upscale_hospital_base_colors.py", content / "shared/render/gameplay/textures", ACTIVE_SKINS,
                  stage / "ActiveSkins")
    preview = {
        "map": "MounE",
        "source_sha256": originals,
        "prepared_sha256": active_files(stage),
        "vending_atlas_sha256": sha256(VENDING_ATLAS),
        "weights_sha256": sha256(args.weights) if args.weights is not None else None,
    }
    (stage / "preview-inputs.json").write_text(json.dumps(preview, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("content", type=Path)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--weights", type=Path)
    parser.add_argument("--neural-python")
    parser.add_argument("--threads", type=int, default=3)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    if not VENDING_ATLAS.is_file():
        parser.error(f"The HD Mount Hospital preview requires the approved local vending cabinet atlas at {VENDING_ATLAS}")
    if args.threads < 1:
        parser.error("--threads must be at least 1")
    content, stage = args.content.resolve(strict=True), args.stage.resolve()
    if stage == content or content in stage.parents or stage in content.parents:
        parser.error("Choose a preview stage outside the converted content")
    project_content = (TOOLS.parents[1] / "Content").resolve()
    if stage == project_content or project_content in stage.parents:
        parser.error("Choose a preview stage outside the project Content directory")
    render = content / "svmap/MounE/render"
    receipt = stage / "preview-inputs.json"
    if args.validate_only and not receipt.is_file():
        parser.error("Prepare the preview stage before validating it")
    originals = stock_files(content, render)
    if not args.validate_only and not receipt.is_file():
        prepare(content, render, stage, args, originals)
    preview = read(receipt)
    if preview.get("vending_atlas_sha256") != sha256(VENDING_ATLAS):
        raise ValueError("The vending cabinet artwork changed. Prepare the preview in a new empty stage.")
    if (preview["map"] != "MounE" or preview["source_sha256"] != originals
            or preview["prepared_sha256"] != active_files(stage)):
        raise ValueError("The staged preview differs from its source or its preparation receipt")
    if args.weights is not None and preview["weights_sha256"] != sha256(args.weights):
        raise ValueError("The staged preview differs from the requested neural checkpoint")
    print(f"Validated {len(originals)} source files and {len(preview['prepared_sha256'])} preview inputs")


if __name__ == "__main__":
    main()
