"""Apply the six prepared glow masks to the copied Hospital's seven existing mask slots."""
import argparse
import json
from pathlib import Path
import re

import unreal

from apply_hospital_lightmaps import ASSETS, MAP, MATERIAL, check_png, log, object_path, path_of, size_of


DESTINATION = MAP + "/HospitalGlowMasks"


def parameter(instance, name):
    return MATERIAL.get_material_instance_texture_parameter_value(instance, name)


def import_masks(manifest, stock_images):
    tasks = {}
    for file, (stock, expected_size) in stock_images.items():
        path = manifest.parent / file
        if check_png(path, stock) != expected_size:
            raise ValueError(f"{file}: the prepared mask is missing or has the wrong dimensions")
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = DESTINATION
        task.automated = True
        task.save = False
        task.replace_existing = True
        task.replace_existing_settings = True
        tasks[file] = task
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(list(tasks.values()))

    masks = {}
    for file, task in tasks.items():
        stock, expected_size = stock_images[file]
        expected = object_path(DESTINATION, Path(file).stem)
        mask = next((asset for asset in task.get_objects() if path_of(asset) == expected), None)
        if not isinstance(mask, unreal.Texture2D) or size_of(mask) != expected_size:
            raise ValueError(f"{file}: could not import the mask at {expected_size}")
        for prop in ("srgb", "address_x", "address_y", "filter", "lod_group", "lod_bias", "never_stream",
                     "mip_gen_settings", "preserve_border"):
            mask.set_editor_property(prop, stock.get_editor_property(prop))
        # The material reads alpha independently of the colour transfer function.
        mask.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        mask.set_editor_property("compression_no_alpha", False)
        mask.set_editor_property("virtual_texture_streaming", False)
        if not ASSETS.save_loaded_asset(mask, only_if_is_dirty=False):
            raise RuntimeError(f"{file}: could not save the mask")
        masks[file] = mask
    return masks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--restore", action="store_true")
    args = parser.parse_args()
    plan = json.loads(args.manifest.read_text(encoding="utf-8"))
    if plan["map"] != MAP or len(plan["bindings"]) != 7 or len(plan["images"]) != 6:
        raise ValueError("Expected the Enhanced Mount Hospital glow-mask plan")

    stock_images = {}
    bindings = []
    for path, row in plan["bindings"].items():
        file = row["file"]
        if not re.fullmatch(r"[A-Za-z0-9_]+_SelfIllumMask\.png", file) or file not in plan["images"]:
            raise ValueError(f"Unexpected glow-mask filename: {file}")
        if not path.startswith(MAP + "/Materials/") or not row["stock"].startswith((MAP + "/Extra/", MAP + "/world/Textures/")):
            raise ValueError(f"{path}: the plan would edit a binding outside the copied map")
        target = object_path(DESTINATION, Path(file).stem)
        instance = unreal.load_asset(path)
        stock = unreal.load_asset(row["stock"])
        if not isinstance(instance, unreal.MaterialInstanceConstant) or not isinstance(stock, unreal.Texture2D):
            raise ValueError(f"{path}: the original texture or owned material is missing")
        if path_of(parameter(instance, "SelfIllumMask")) not in (row["stock"], target):
            raise ValueError(f"{path}: mask changed outside this import")
        glow = parameter(instance, "SelfIllumTexture")
        size = tuple(plan["images"][file]["size"])
        if not args.restore and (path_of(glow) != row["glow"] or size_of(glow) != size):
            raise ValueError(f"{path}: glow image changed after mask preparation")
        previous = stock_images.setdefault(file, (stock, size))
        if previous != (stock, size):
            raise ValueError(f"{file}: different stock textures share one prepared filename")
        bindings.append((instance, file))

    masks = {file: stock for file, (stock, _) in stock_images.items()} if args.restore else import_masks(args.manifest, stock_images)
    for instance, file in bindings:
        mask = masks[file]
        if parameter(instance, "SelfIllumMask") != mask:
            MATERIAL.set_material_instance_texture_parameter_value(instance, "SelfIllumMask", mask)
            if parameter(instance, "SelfIllumMask") != mask or not ASSETS.save_loaded_asset(instance, only_if_is_dirty=False):
                raise RuntimeError(f"{instance.get_name()}: could not save SelfIllumMask")
        log(f"{instance.get_name()}.SelfIllumMask -> {path_of(mask)}")
    log(f"{'Restored' if args.restore else 'Updated'} {len(bindings)} glow-mask bindings from {len(masks)} texture assets")


if __name__ == "__main__":
    main()
