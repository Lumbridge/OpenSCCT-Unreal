"""Build the detachable weapon surfaces from OpenSCCT's own sight and suppressor art."""
import json
from pathlib import Path
import struct

import unreal as ue

from scct_materials import FLOAT1, FLOAT3, Graph
from scct_rifle_cosmetics import build as rifle_surface


FOLDER = "/Game/OpenSCCT/Weapons/Materials/Attachments"
SOURCE = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir())) / "Runtime/content/assets/rifles/export"


def finish(name):
    material = ue.load_asset(FOLDER + "/OpenScctRifles_" + name)
    if not material:
        raise RuntimeError(f"Missing authored attachment surface {name}; run scct_attachment_finishes.py")
    return material


def source_materials(model):
    with (SOURCE / model).open("rb") as stream:
        stream.seek(12)
        size, _ = struct.unpack("<II", stream.read(8))
        doc = json.loads(stream.read(size))
    for mat in doc["materials"]:
        name = mat["name"].removeprefix("OpenScctRifles.")
        if name.startswith("Optic") or name == "Suppressor":
            tex = mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]
            yield name, mat, SOURCE / doc["images"][doc["textures"][tex]["source"]]["uri"]


def build_surface(name, mat, source):
    task = ue.AssetImportTask()
    task.filename = str(source)
    task.destination_path = FOLDER + "/Textures"
    task.factory = ue.TextureFactory()
    task.automated = task.replace_existing = task.save = True
    ue.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = ue.load_asset(task.destination_path + "/" + source.stem)
    if not texture:
        raise RuntimeError("Could not import " + str(source))
    label = "OpenScctRifles_" + name
    path = FOLDER + "/" + label
    pbr = mat["pbrMetallicRoughness"]
    if name in ("OpticLens", "OpticReticle"):
        g = Graph(path, rebuild_in_place=True)
        sample = g.texture("BaseColorTexture", texture.get_path_name())
        g.material.set_editor_property("two_sided", True)
        prop = ue.MaterialProperty
        if name == "OpticLens":
            g.material.set_editor_property("blend_mode", ue.BlendMode.BLEND_TRANSLUCENT)
            g.material.set_editor_property("translucency_lighting_mode", ue.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
            g.output(sample, prop.MP_BASE_COLOR, "RGB")
            factor = g.scalar("OpacityFactor", pbr["baseColorFactor"][3])
            g.output(g.custom("return Alpha * Factor;", FLOAT1, Alpha=(sample, "A"), Factor=factor), prop.MP_OPACITY)
            g.output(g.scalar("MetallicFactor", pbr["metallicFactor"]), prop.MP_METALLIC)
            g.output(g.scalar("RoughnessFactor", pbr["roughnessFactor"]), prop.MP_ROUGHNESS)
        else:
            g.material.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_UNLIT)
            colour = g.colour("EmissiveFactor", (*mat["emissiveFactor"], 1))
            g.output(g.in_the_picture(g.custom("return Base + Emission;", FLOAT3, Base=(sample, "RGB"), Emission=colour)), prop.MP_EMISSIVE_COLOR)
        return g.finish()
    lib = ue.MaterialEditingLibrary
    material = (ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else
                ue.AssetToolsHelpers.get_asset_tools().create_asset(label, FOLDER, ue.MaterialInstanceConstant, ue.MaterialInstanceConstantFactoryNew()))
    lib.set_material_instance_parent(material, rifle_surface())
    lib.set_material_instance_texture_parameter_value(material, "BaseColorTexture", texture)
    for key in ("metallicFactor", "roughnessFactor"):
        lib.set_material_instance_scalar_parameter_value(material, key[0].upper() + key[1:], pbr[key])
    overrides = material.get_editor_property("base_property_overrides")
    overrides.set_editor_property("override_two_sided", True)
    overrides.set_editor_property("two_sided", mat.get("doubleSided", False))
    material.set_editor_property("base_property_overrides", overrides)
    lib.update_material_instance(material)
    if not ue.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + path)
    return material


def build():
    surfaces = {}
    # TextureFactory keeps a project-relative reimport path. Interchange also serializes
    # its machine-specific factory-node ID into the uncooked asset.
    setting = "Interchange.FeatureFlags.Import.PNG"
    previous = ue.SystemLibrary.get_console_variable_bool_value(setting)
    world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    ue.SystemLibrary.execute_console_command(world, setting + " 0")
    try:
        for name, mat, source in (*source_materials("micro_red_dot.glb"), *source_materials("m4a1s.glb")):
            if name not in surfaces:
                surface = build_surface(name, mat, source)
                surfaces[surface.get_name()] = surface
    finally:
        ue.SystemLibrary.execute_console_command(world, f"{setting} {int(previous)}")
    directory = "/Game/OpenSCCT/Weapons"
    for path in ue.EditorAssetLibrary.list_assets(directory, recursive=False):
        if not path.rsplit("/", 1)[-1].startswith("SM_Merc"):
            continue
        mesh = ue.load_asset(path)
        slots = mesh.get_editor_property("static_materials")
        changed = False
        for index, slot in enumerate(slots):
            surface = surfaces.get(str(slot.material_slot_name))
            if surface and slot.material_interface != surface:
                slot.material_interface = surface
                slots[index] = slot
                changed = True
            if not slot.material_interface:
                raise RuntimeError(f"Unresolved surface on {path}: {slot.material_slot_name}")
        if changed:
            mesh.set_editor_property("static_materials", slots)
            if not ue.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError("Could not save " + path)
            ue.log_warning("SCCT-IMPORT restored surfaces on " + path)


if __name__ == "__main__":
    build()
