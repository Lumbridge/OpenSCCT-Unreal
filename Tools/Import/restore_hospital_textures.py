"""Regenerate the source-faithful Mount Hospital texture stage from an installed map."""

import argparse
from pathlib import Path
import subprocess
import sys

import cv2

import restore_hospital_atmosphere
import restore_hospital_graffiti
import restore_hospital_original_colors
import restore_hospital_original_sewer
import restore_hospital_sewer_surfaces
import restore_hospital_signs
import stage_hospital_restoration
import upscale_hospital_base_colors
from restore_hospital_original_colors import read
from upscale_hospital_base_colors import sha256


TOOLS = Path(__file__).resolve().parent
INVENTORY = TOOLS / "hospital-restoration-inputs.json"


def validate_source(render, inventory):
    if sha256(render / "materials.json") != inventory["converted_materials_sha256"]:
        raise ValueError("The installed MounE material inventory differs from the audited map")
    source = render / "textures"
    for name, record in inventory["textures"].items():
        if sha256(source / name) != record["sha256"]:
            raise ValueError(f"The installed MounE PNG differs from the audited original: {name}")
    return source


def run(args):
    map_folder = args.source.resolve(strict=True)
    stage = args.stage.resolve()
    if stage == map_folder or map_folder in stage.parents:
        raise ValueError("Stage outside the installed MounE map")
    if stage.exists() and (not stage.is_dir() or any(stage.iterdir())):
        raise ValueError(f"Choose an empty staging directory: {stage}")

    render = map_folder / "render"
    inventory = read(INVENTORY)
    source = validate_source(render, inventory)
    stage.mkdir(parents=True, exist_ok=True)
    cv2.setNumThreads(args.threads)

    prior = stage / "SourceGuidance"
    if args.weights is None:
        upscale_hospital_base_colors.build(source, INVENTORY, prior, None, args.threads)
    else:
        subprocess.run(
            [args.neural_python, "-B", str(TOOLS / "upscale_hospital_base_colors.py"),
             str(source), str(INVENTORY), str(prior), "--weights", str(args.weights.resolve(strict=True)),
             "--threads", str(args.threads)],
            check=True,
        )

    colours = stage / "OriginalMapColors"
    restore_hospital_original_colors.build(source, prior / "manifest.json", colours)

    specialists = stage / "Specialists"
    signs = specialists / "OriginalSignRestoration"
    restore_hospital_graffiti.run(source, specialists / "OriginalGraffiti")
    restore_hospital_signs.run(source, signs)
    restore_hospital_sewer_surfaces.run(source, specialists / "OriginalSewerSurfaces", prior,
                                        signs / restore_hospital_signs.GRILLE)
    restore_hospital_atmosphere.run(source, specialists / "OriginalAtmosphere")
    restore_hospital_original_sewer.run(render, specialists / "OriginalSewer")
    props = specialists / "OriginalProps"
    subprocess.run(
        ["pwsh", "-NoProfile", "-File", str(TOOLS / "Restore-HospitalOriginalProps.ps1"),
         "-OriginalDirectory", str(source), "-OutputDirectory", str(props)],
        check=True,
    )

    restored = stage / "OriginalRestoration"
    stage_hospital_restoration.build(source, colours, specialists, inventory["glow_masks"], restored)
    print(f"Colour manifest ready: {restored / 'manifest.json'}")
    print(f"Glow-mask manifest ready: {restored / 'GlowMasks' / 'self-illum-masks.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Installed converted svmap/MounE directory")
    parser.add_argument("stage", type=Path, help="New or empty staging directory outside the map")
    parser.add_argument("--weights", type=Path, help="Official Real-ESRGAN general x4v3 checkpoint")
    parser.add_argument("--neural-python", default=sys.executable, help="Python with CPU PyTorch for --weights")
    parser.add_argument("--threads", type=int, default=3, help="OpenCV and optional CPU inference threads")
    args = parser.parse_args()
    if args.threads < 1:
        parser.error("--threads must be at least 1")
    run(args)


if __name__ == "__main__":
    main()
