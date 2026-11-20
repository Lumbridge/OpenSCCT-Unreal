"""Apply an isolated Mount Hospital colour and glow pass, including the drain's relief."""

from pathlib import Path
import sys

import unreal

import apply_hospital_base_colors as colours
import apply_hospital_self_illum_masks as masks
from apply_hospital_lightmaps import ASSETS, MAP, MATERIAL, log, path_of


def apply_drain_normal(source):
    name = "Prison_TEX_plaquegout_N"
    folder = MAP + "/HospitalSurfaceNormals"
    asset_path = f"{folder}/{name}.{name}"
    if source.name != name + ".png":
        raise ValueError("The authored drain normal has the wrong filename")
    material = unreal.load_asset(MAP + "/Materials/MI_Prison_TEX_plaquegout")
    if not isinstance(material, unreal.MaterialInstanceConstant):
        raise ValueError("The Hospital drain material is missing")

    previous = MATERIAL.get_material_instance_texture_parameter_value(material, "Normal")
    if previous is None or not path_of(previous).startswith((MAP + "/Normals/", folder + "/")):
        raise ValueError(f"Unexpected Hospital drain normal: {path_of(previous)}")
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.automated = True
    task.save = False
    task.replace_existing = True
    task.replace_existing_settings = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    normal = next((asset for asset in task.get_objects() if path_of(asset) == asset_path), None)
    if not isinstance(normal, unreal.Texture2D):
        raise ValueError("The authored drain relief did not import")
    normal.set_editor_property("srgb", False)
    normal.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    normal.set_editor_property("lod_group", previous.get_editor_property("lod_group"))
    if not ASSETS.save_loaded_asset(normal, only_if_is_dirty=False):
        raise ValueError("The authored drain relief could not be saved")
    MATERIAL.set_material_instance_texture_parameter_value(material, "Normal", normal)
    if (MATERIAL.get_material_instance_texture_parameter_value(material, "Normal") != normal
            or not ASSETS.save_loaded_asset(material, only_if_is_dirty=False)):
        raise ValueError("The drain material did not retain the new relief")
    log(f"Verified {material.get_name()}.Normal -> {path_of(normal)}")


if __name__ == "__main__":
    colour_plan, colour_rollback, mask_plan, drain_normal = map(Path, sys.argv[1:])
    colours.apply(colour_plan, colour_rollback)
    sys.argv = [sys.argv[0], str(mask_plan)]
    masks.main()
    apply_drain_normal(drain_normal)
