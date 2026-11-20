"""Reimport only the live Mount Hospital carton atlas without changing its LOD policy."""
from pathlib import Path
import sys

import unreal


LEVEL = "/Game/Imported/Maps/MounE"
NAME = "T_MounE_Prison_TEX_Box1_Base"
DESTINATION = LEVEL + "/HospitalBaseColors"
ASSET = DESTINATION + "/" + NAME
MATERIAL = LEVEL + "/Materials/MI_Prison_TEX_Box1"
IMPORT_SETTINGS = ("srgb", "compression_settings", "compression_no_alpha", "address_x", "address_y", "filter",
                   "virtual_texture_streaming", "lod_group", "mip_gen_settings", "never_stream", "lod_bias",
                   "max_texture_size")


def main(image):
    if image.name != NAME + ".png" or not image.is_file():
        raise ValueError("Expected the prepared carton PNG for the existing atlas name")
    editor = unreal.EditorAssetLibrary
    material = unreal.load_asset(MATERIAL)
    texture = unreal.load_asset(ASSET)
    editing = unreal.MaterialEditingLibrary
    if not isinstance(texture, unreal.Texture2D) or not isinstance(material, unreal.MaterialInstanceConstant):
        raise RuntimeError("Apply the main Mount Hospital colour restoration before refreshing the carton")
    bound = editing.get_material_instance_texture_parameter_value(material, "BaseColor")
    if bound.get_path_name() != texture.get_path_name():
        raise RuntimeError("The carton material is not drawing the expected atlas")
    properties = {name: texture.get_editor_property(name) for name in IMPORT_SETTINGS}

    task = unreal.AssetImportTask()
    task.filename = str(image.resolve())
    task.destination_path = DESTINATION
    task.replace_existing = True
    task.replace_existing_settings = False
    task.automated = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    restored = unreal.load_asset(ASSET)
    if not task.imported_object_paths or not isinstance(restored, unreal.Texture2D):
        raise RuntimeError("The carton texture did not reimport")
    before_saving = tuple(restored.get_editor_property(name) == value for name, value in properties.items())
    for (name, value), matches in zip(properties.items(), before_saving, strict=True):
        if not matches:
            restored.set_editor_property(name, value)
    if not all(restored.get_editor_property(name) == value for name, value in properties.items()):
        raise RuntimeError("The carton texture did not retain the existing import settings")
    actual = editing.get_material_instance_texture_parameter_value(material, "BaseColor")
    if actual.get_path_name() != restored.get_path_name():
        raise RuntimeError("The existing carton material binding changed unexpectedly")
    size = (restored.blueprint_get_size_x(), restored.blueprint_get_size_y())
    if size != (2048, 2048):
        raise RuntimeError(f"The new carton texture has an unexpected size: {size}")
    if not editor.save_loaded_asset(restored, only_if_is_dirty=False):
        raise RuntimeError("Saving the restored carton texture failed")
    unreal.log_warning(f"SCCT-IMPORT {restored.get_path_name()} reimported at {size[0]}x{size[1]}, "
                       f"kept {properties['lod_group']} with original mip policy and live {material.get_name()} binding")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
