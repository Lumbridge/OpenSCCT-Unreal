"""Bind upscaled colour textures to the copied Mount Hospital map's existing materials."""
import json
from pathlib import Path
import re
import sys

import unreal


MAP = "/Game/Imported/Maps/MounE"
MATERIAL_FOLDER = MAP + "/Materials"
DESTINATION = MAP + "/HospitalBaseColors"
COLOUR_PARAMETERS = ("BaseColor", "SelfIllumTexture", "OverlayTexture", *(f"SeqTex{i}" for i in range(9)))
ASSETS = unreal.EditorAssetLibrary
MATERIAL = unreal.MaterialEditingLibrary


def log(message):
    unreal.log_warning("SCCT-IMPORT " + message)


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def color_at(instance, parameter):
    return MATERIAL.get_material_instance_texture_parameter_value(instance, parameter) if instance else None


def set_color(instance, parameter, texture):
    existing = color_at(instance, parameter)
    if existing and existing.get_path_name() == texture.get_path_name():
        return False
    # UE 5.8 changes the binding but always reports false from this setter.
    MATERIAL.set_material_instance_texture_parameter_value(instance, parameter, texture)
    actual = color_at(instance, parameter)
    if not actual or actual.get_path_name() != texture.get_path_name():
        raise RuntimeError(f"{instance.get_name()}: {parameter} binding did not update")
    return True


def save_bindings(instance, textures):
    changed = [set_color(instance, parameter, texture) for parameter, texture in textures.items()]
    if any(changed) and not ASSETS.save_loaded_asset(instance, only_if_is_dirty=False):
        raise RuntimeError(f"{instance.get_name()}: could not save colour bindings")


def load_materials():
    result = {}
    for path in ASSETS.list_assets(MATERIAL_FOLDER, recursive=False):
        instance = unreal.load_asset(path)
        if isinstance(instance, unreal.MaterialInstanceConstant):
            result[str(instance.get_name())] = instance
    return result


def import_textures(plan_path, selections, originals):
    return import_textures_from_directory(plan_path.parent, selections, originals)


def import_textures_from_directory(directory, selections, originals):
    group = next(getattr(unreal.TextureGroup, prop) for prop in dir(unreal.TextureGroup)
                 if prop.lower().replace("_", "") == "texturegroupproject01")
    tasks = []
    for key in sorted(originals):
        file = selections[key]["file"]
        if Path(file).name != file or not (directory / file).is_file():
            raise ValueError("Missing prepared colour image: " + file)
        task = unreal.AssetImportTask()
        task.filename = str(directory / file)
        task.destination_path = DESTINATION
        task.automated = True
        task.save = False
        task.replace_existing = True
        task.replace_existing_settings = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    imported = {}
    for key, original in originals.items():
        texture = unreal.load_asset(f"{DESTINATION}/{Path(selections[key]['file']).stem}")
        if not isinstance(texture, unreal.Texture2D):
            raise ValueError(f"Missing imported colour image for {selections[key]['source']}")
        for prop in ("srgb", "compression_settings", "compression_no_alpha", "address_x",
                     "address_y", "filter", "virtual_texture_streaming"):
            texture.set_editor_property(prop, original.get_editor_property(prop))
        texture.set_editor_property("lod_group", group)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        texture.set_editor_property("never_stream", False)
        if not ASSETS.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {texture.get_name()}")
        imported[key] = texture
    return imported


def apply(plan_path, snapshot_path):
    plan = read(plan_path)
    if plan.get("map") != "MounE":
        raise ValueError("Colour import plan belongs to another map")
    selections = plan["textures"]
    active = read(snapshot_path) if snapshot_path.is_file() else {"map": MAP, "bindings": {}}
    if active.get("map") != MAP:
        raise ValueError("Colour rollback belongs to another map")
    prior = active["bindings"]
    instances = load_materials()
    originals = {}
    targets = {}
    coverage = {"matched": {}, "no_replacement": {}}
    for name, instance in instances.items():
        for parameter in COLOUR_PARAMETERS:
            live = color_at(instance, parameter)
            saved = prior.get(name, {}).get(parameter)
            if saved and (not live or live.get_path_name() not in saved):
                raise ValueError(f"{name}.{parameter}: changed outside the colour import")
            stock = unreal.load_asset(saved[0]) if saved else live
            if not isinstance(stock, unreal.Texture2D) or not stock.get_path_name().startswith(
                    (MAP + "/world/Textures/", MAP + "/Extra/")):
                continue
            key = re.sub(r"[^a-z0-9]", "", stock.get_name().lower())
            if key not in selections:
                coverage["no_replacement"].setdefault(stock.get_path_name(), []).append(f"{name}.{parameter}")
                continue
            stem = Path(selections[key]["file"]).stem
            replacement = f"{DESTINATION}/{stem}.{stem}"
            if saved and saved[1] != replacement:
                raise ValueError(f"{name}.{parameter}: replacement asset changed. Restore before changing the plan.")
            targets.setdefault(name, {})[parameter] = (key, [stock.get_path_name(), replacement])
            originals[key] = stock
            coverage["matched"].setdefault(selections[key]["source"], []).append({
                "material": name, "parameter": parameter, "blend": str(instance.get_blend_mode()),
                "stock": stock.get_path_name(), "replacement": replacement,
            })

    for name, parameters in prior.items():
        if not parameters.keys() <= targets.get(name, {}).keys():
            raise ValueError("The plan removed active colour bindings. Restore before changing the plan.")
    coverage["no_live_color_binding"] = [selections[key]["source"] for key in sorted(selections.keys() - originals.keys())]
    save_json(plan_path.with_name("base-color-coverage.json"), coverage)
    if not targets:
        raise ValueError("No Mount Hospital colour targets matched the live map")

    bindings = {name: {p: paths for p, (_, paths) in values.items()} for name, values in targets.items()}
    save_json(snapshot_path, {"map": MAP, "bindings": bindings})
    imported = import_textures(plan_path, selections, originals)
    for name, values in targets.items():
        save_bindings(instances[name], {p: imported[key] for p, (key, _) in values.items()})
    count = sum(map(len, bindings.values()))
    log(f"Applied {len(imported)} streaming map textures to {count} colour bindings in {len(targets)} materials")
    log(f"Coverage: {len(coverage['no_replacement'])} original map texture assets lack a replacement, "
        f"{len(coverage['no_live_color_binding'])} inputs have no matching colour binding")


def restore(snapshot_path):
    if not snapshot_path.is_file():
        log("No active Mount Hospital colour bindings to restore")
        return
    snapshot = read(snapshot_path)
    if snapshot.get("map") != MAP:
        raise ValueError("Colour rollback belongs to another map")
    instances = load_materials()
    values = {}
    for name, parameters in snapshot["bindings"].items():
        for parameter, paths in parameters.items():
            if parameter not in COLOUR_PARAMETERS:
                raise ValueError("Unexpected colour parameter in rollback: " + parameter)
            live = color_at(instances.get(name), parameter)
            if not live or live.get_path_name() not in paths:
                raise ValueError(f"{name}.{parameter}: changed outside the colour import")
            stock = unreal.load_asset(paths[0])
            if not isinstance(stock, unreal.Texture2D):
                raise ValueError(f"{name}.{parameter}: original texture is missing")
            values.setdefault(name, {})[parameter] = stock
    for name, textures in values.items():
        save_bindings(instances[name], textures)
    snapshot_path.replace(snapshot_path.with_name("base-color-rollback.restored.json"))
    log(f"Restored {sum(map(len, values.values()))} original Hospital colour bindings")


if __name__ == "__main__":
    operation, plan_path, snapshot_path = sys.argv[1:]
    if operation == "apply":
        apply(Path(plan_path), Path(snapshot_path))
    elif operation == "restore":
        restore(Path(snapshot_path))
    else:
        raise ValueError("Expected apply or restore")
