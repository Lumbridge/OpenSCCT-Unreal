"""Build a local game-rendered texture review of the eleven carried assets."""

import json
from pathlib import Path
import sys

import unreal

from apply_hospital_lightmaps import ASSETS, MATERIAL, log


FOLDER = "/Game/HospitalTextureReview"
LEVEL = FOLDER + "/TextureReview"
SOURCE_NAMES = (
    "MHOsfx_xbox_rayonlum", "MHOtex_xbox_Envmapvitre", "Prison_TEX_Box1",
    "Prison_TEX_Tsky04", "Prison_TEX_Tsky05", "Prison_TEX_lune",
    "Prison_TEX_mh_buildingblack", "Prison_TEX_starsky",
    "Prison_TEX_warp_skyline1", "Prison_TEX_warp_skyline_sol", "Prison_TEX_water",
)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def save(asset):
    if not ASSETS.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def master(texture):
    asset = TOOLS.create_asset("M_TextureReview", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    asset.set_editor_property("two_sided", True)
    asset.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    sample = MATERIAL.create_material_expression(asset, unreal.MaterialExpressionTextureSampleParameter2D, -290, 0)
    sample.set_editor_property("parameter_name", "ReviewImage")
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if not MATERIAL.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Could not connect the texture review material")
    MATERIAL.recompile_material(asset)
    save(asset)
    return asset


def panel(actors, mesh, parent, name, texture, y):
    material = TOOLS.create_asset(name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MATERIAL.set_material_instance_parent(material, parent)
    MATERIAL.set_material_instance_texture_parameter_value(material, "ReviewImage", texture)
    if MATERIAL.get_material_instance_texture_parameter_value(material, "ReviewImage") != texture:
        raise RuntimeError(f"{name}: could not set the source texture for review")
    save(material)
    actor = actors.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(400, y, 0), unreal.Rotator(roll=90, yaw=90))
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    component.set_material(0, material)
    width, height = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    pixel_ratio = width / height
    # Normalize the longest side for a consistent close look without changing aspect.
    scale_x = 3.7 * min(pixel_ratio, 1.0)
    scale_y = 3.7 * min(1.0 / pixel_ratio, 1.0)
    actor.set_actor_scale3d(unreal.Vector(scale_x, scale_y, 1))
    actor.set_actor_label(name)


def build(snapshot, receipt):
    rows = json.loads(snapshot.read_text(encoding="utf-8"))["bindings"]
    mapping = {}
    for material in rows.values():
        for original, restored in material.values():
            name = original.rsplit(".", 1)[-1]
            if name in SOURCE_NAMES:
                mapping[name] = (original, restored)
    if set(mapping) != set(SOURCE_NAMES):
        raise ValueError("The eleven texture review assets must have exact original and restored paths")
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        raise RuntimeError("Could not create the local review map")
    engine_plane = unreal.load_asset("/Engine/BasicShapes/Plane.Plane")
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    parent = master(unreal.load_asset(mapping[SOURCE_NAMES[0]][1]))
    captures = []
    for index, name in enumerate(SOURCE_NAMES):
        center = index * 1900
        paths = mapping[name]
        for suffix, path, offset in zip(("Before", "Restored"), paths, (-218, 218), strict=True):
            asset = unreal.load_asset(path)
            if not isinstance(asset, unreal.Texture2D):
                raise ValueError(f"Missing texture {path}")
            panel(actor_subsystem, engine_plane, parent, f"T{index:02}_{suffix}", asset, center + offset)
        captures.append({"name": name, "camera": f"0,{center},0,0,0", "original": paths[0], "restored": paths[1]})
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, LEVEL):
        raise RuntimeError("Could not save the local review map")
    receipt.write_text(json.dumps({"level": LEVEL, "shots": captures}, indent=2) + "\n", encoding="utf-8")
    log(f"Saved a game-rendered review stage for {len(captures)} original/restored texture pairs")


if __name__ == "__main__":
    build(*(Path(arg) for arg in sys.argv[1:]))
