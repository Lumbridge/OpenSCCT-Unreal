"""Import Mount Hospital's shared-prop colours without changing shared packages."""

import json
from pathlib import Path
import sys

import unreal


MAP = "/Game/Imported/Maps/MounE"
SHARED = "/Game/Imported/Fx/SGameplayObjects"
COLORS = MAP + "/HospitalSharedColors"
MATERIALS = MAP + "/HospitalSharedMaterials"
ASSETS = unreal.EditorAssetLibrary
MATERIAL = unreal.MaterialEditingLibrary
TEXTURE_PROPERTIES = (
    "srgb", "compression_settings", "compression_no_alpha", "address_x", "address_y", "filter",
    "virtual_texture_streaming",
)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def require_local_destination():
    content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).absolute()
    map_dir = content / "Imported/Maps/MounE"
    if not map_dir.is_dir():
        raise RuntimeError(f"Import the copied Enhanced Mount Hospital map first: {map_dir}")
    for path in [*list(map_dir.parents)[:3], map_dir, map_dir / "HospitalSharedColors", map_dir / "HospitalSharedMaterials"]:
        if path.exists() and getattr(path.lstat(), "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"Use an independent copy before importing into {path}")


def staged_colors(audit, color_manifest):
    data = read_json(audit)
    if data.get("map") != "MounE":
        raise ValueError("Expected an Enhanced Mount Hospital shared-prop audit")
    inputs = read_json(color_manifest)
    if set(inputs) != set(data["textures"]):
        raise ValueError("The audit and colour manifest differ")
    for basename, entry in data["textures"].items():
        if Path(basename).name != basename or Path(basename).suffix.lower() != ".png":
            raise ValueError(f"Invalid original texture filename {basename}")
        output_name = inputs[basename]
        replacement = color_manifest.parent / output_name
        if Path(output_name).name != output_name or replacement.suffix.lower() != ".png" or not replacement.is_file():
            raise ValueError(f"Invalid staged replacement for {basename}")
        yield Path(basename).stem, entry, replacement


def prepare_bindings(audit, color_manifest):
    result = {}
    for name, entry, replacement in staged_colors(audit, color_manifest):
        original = ASSETS.load_asset(SHARED + "/Materials/" + name)
        if not isinstance(original, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"Original glTF material {name} is unavailable")
        base_texture = MATERIAL.get_material_instance_texture_parameter_value(original, "BaseColorTexture")
        if not isinstance(base_texture, unreal.Texture2D) or base_texture.get_name() != name:
            raise RuntimeError(f"Unexpected original base colour for {original.get_path_name()}")
        parameters = ["BaseColorTexture"]
        if any(use["role"] == "Self-illumination image" for use in entry["uses"]):
            glow = MATERIAL.get_material_instance_texture_parameter_value(original, "EmissiveTexture")
            if not glow or glow.get_path_name() != base_texture.get_path_name():
                raise RuntimeError(f"Unexpected emissive alias on {original.get_path_name()}")
            parameters.append("EmissiveTexture")
        result[name] = (original, base_texture, replacement, parameters, entry["size"])
    return result


def import_textures(bindings):
    tasks = []
    for name, (_, _, image, _, _) in bindings.items():
        task = unreal.AssetImportTask()
        task.filename = str(image)
        task.destination_path = COLORS
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    group = next(value for value in dir(unreal.TextureGroup) if value.lower().replace("_", "") == "texturegroupproject01")
    for name, (_, original, _, _, size) in bindings.items():
        texture = ASSETS.load_asset(COLORS + "/" + name)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"Failed to import the staged texture {name}")
        width, height = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
        if [width, height] != [value * 4 for value in size]:
            raise RuntimeError(f"Unexpected imported resolution for {name}: {width} x {height}")
        if original is not None:
            for prop in TEXTURE_PROPERTIES:
                texture.set_editor_property(prop, original.get_editor_property(prop))
        else:
            texture.set_editor_property("srgb", True)
        texture.set_editor_property("lod_group", getattr(unreal.TextureGroup, group))
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        texture.set_editor_property("never_stream", False)
        if not ASSETS.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save {texture.get_path_name()}")
        yield name, texture


def apply(audit, color_manifest, active_audit=None, active_manifest=None):
    require_local_destination()
    bindings = prepare_bindings(audit, color_manifest)
    active_colors = {} if active_audit is None else {
        name: (None, None, replacement, (), entry["size"])
        for name, entry, replacement in staged_colors(active_audit, active_manifest)
    }
    if bindings.keys() & active_colors.keys():
        raise ValueError("A colour is declared both as a shared material and an active skin")
    textures = dict(import_textures({**bindings, **active_colors}))
    for name, (original, _, _, parameters, _) in bindings.items():
        destination = MATERIALS + "/" + name
        instance = ASSETS.load_asset(destination) if ASSETS.does_asset_exist(destination) else ASSETS.duplicate_asset(original.get_path_name(), destination)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"Failed to create the map-local material {destination}")
        for parameter in parameters:
            MATERIAL.set_material_instance_texture_parameter_value(instance, parameter, textures[name])
            actual = MATERIAL.get_material_instance_texture_parameter_value(instance, parameter)
            if not actual or actual.get_path_name() != textures[name].get_path_name():
                raise RuntimeError(f"Failed readback for {destination}.{parameter}")
        if not ASSETS.save_loaded_asset(instance, only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save {destination}")
    unreal.log_warning(f"SCCT-IMPORT Bound {len(bindings)} map-local shared-prop materials to 4x colour textures")
    unreal.log_warning(f"SCCT-IMPORT Imported {len(active_colors)} map-local active skin colour textures")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 5):
        raise ValueError("Pass the shared-prop audit and staged colour manifest, and optionally the active skin audit and manifest")
    apply(*(Path(value) for value in sys.argv[1:]))
