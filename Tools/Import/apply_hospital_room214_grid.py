"""Bind the upper-hall roof aperture to its existing native projector."""

import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

import unreal

from scct_effects import LIGHT_FUNCTION


DEST = "/Game/Imported/Maps/MounE/Effects/Room214"
TEXTURE = "T_MounE_Room214Roof"
MATERIAL = "MI_MounE_Room214Roof"


def build_assets(render, light):
    # The outer Python used by the map import has Pillow and numpy. UE's embedded Python does not.
    staged = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "ScctImport/MounE/Room214"
    image = staged / (TEXTURE + ".png")
    subprocess.run(["python", "-B", str(Path(__file__).with_name("prepare_hospital_room214_grid.py")),
                    str(render), str(image)], check=True)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    assets = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    edit = unreal.MaterialEditingLibrary
    texture_path = f"{DEST}/{TEXTURE}"
    texture = unreal.load_asset(texture_path) if assets.does_asset_exist(texture_path) else None
    if not texture or assets.get_metadata_tag(texture, "ScctRoofMaskSHA256") != digest:
        task = unreal.AssetImportTask()
        task.filename = str(image)
        task.destination_path = DEST
        task.automated = True
        task.replace_existing = True
        tools.import_asset_tasks([task])
        texture = unreal.load_asset(texture_path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("Missing Room 214 roof transmission texture")
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        assets.set_metadata_tag(texture, "ScctRoofMaskSHA256", digest)
        if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError("Could not save the Room 214 roof transmission texture")

    material_path = f"{DEST}/{MATERIAL}"
    material = (unreal.load_asset(material_path) if assets.does_asset_exist(material_path)
                else tools.create_asset(MATERIAL, DEST, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew()))
    edit.set_material_instance_parent(material, unreal.load_asset(LIGHT_FUNCTION))
    # Use the same parent and uniform names as other Echelon projectors. The per-receiver filter
    # copies them when it installs its exclusion mask, including on the panels above the hall.
    width, height = light["spot_width"], light["spot_height"]
    diagonal = math.hypot(width, height)
    for name, value in (("RectU", diagonal / width), ("RectV", diagonal / height), ("CookieWeight", 1)):
        edit.set_material_instance_scalar_parameter_value(material, name, value)
    edit.set_material_instance_texture_parameter_value(material, "Cookie", texture)
    edit.update_material_instance(material)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save the Room 214 projector material")
    return material


def apply_to_fixture(actor, light, map_folder):
    if light["name"] != "Light246" or not isinstance(actor, unreal.SpotLight) or light["effect"] != 22:
        raise ValueError("Expected the original upper-hall projector")
    material = build_assets(Path(map_folder) / "render", light)
    reach = light["echelon_range"]
    behind = reach * light["near_plane_ratio"] / (1.0 - light["near_plane_ratio"])
    extent = math.hypot(light["spot_width"], light["spot_height"])
    angle = math.degrees(math.atan(extent / (reach + behind)))
    pitch, yaw, roll = (v * 360.0 / 65536 for v in light["rotation"])
    rotation = unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)
    apex = unreal.Vector(*light["location"]) - unreal.MathLibrary.get_forward_vector(rotation) * behind
    actor.set_actor_location_and_rotation(apex, rotation, False, True)
    spot = actor.light_component
    spot.set_attenuation_radius(reach + behind)
    spot.set_light_falloff_exponent(0.65)
    spot.set_outer_cone_angle(angle)
    spot.set_inner_cone_angle(angle)
    spot.set_editor_property("light_function_material", material)
    spot.set_editor_property("disabled_brightness", 0.0)
    if "ScctSourceAperture" not in map(str, actor.tags):
        actor.tags = [*actor.tags, unreal.Name("ScctSourceAperture")]


if __name__ == "__main__":
    render = Path(sys.argv[1])
    lights = json.loads((render / "lighting.json").read_text())["lights"]
    build_assets(render, next(light for light in lights if light["name"] == "Light246"))
    unreal.log_warning("SCCT-IMPORT Built the Room 214 roof aperture from the source geometry")
