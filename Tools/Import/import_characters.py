"""Imports the converted character models (spy and merc) as skeletal meshes. Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_characters.py <content root>"

The meshes keep the original skeleton: the simulation's animation is computed for it, and new character
models are driven from it. Goes to /Game/Imported/Chars/ (never committed).
"""
import os
import sys

import unreal

CONTENT_ROOT = sys.argv[1]
CHARS = os.path.join(CONTENT_ROOT, "shared", "chars")
MODELS = [("SPerso", "ATT_01"), ("SPerso", "DEF_01"), ("SPerso", "ATT_01_fp")]


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


tasks = []
for package, mesh in MODELS:
    path = os.path.join(CHARS, package, mesh + ".glb")
    if not os.path.exists(path):
        log("missing " + path)
        continue
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/Imported/Chars/%s/%s/SkeletalMeshes/%s" % (package, mesh, mesh)):
        continue  # already imported; only the materials below are refreshed
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/Game/Imported/Chars/%s" % package
    task.automated = True
    task.save = True
    task.replace_existing = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

# The importer's own materials are not compiled for skinned meshes in a game run, so each slot gets an instance
# of the project's surface material with the slot's original texture.
materials_lib = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
surface = unreal.load_asset("/Game/OpenSCCT/Materials/M_ScctSurface")
if surface and not (surface.get_editor_property("used_with_skeletal_mesh") and surface.get_editor_property("used_with_nanite")):
    surface.set_editor_property("used_with_skeletal_mesh", True)
    surface.set_editor_property("used_with_nanite", True)
    materials_lib.recompile_material(surface)
    unreal.EditorAssetLibrary.save_loaded_asset(surface, only_if_is_dirty=False)

for package, mesh_name in MODELS:
    base = "/Game/Imported/Chars/%s/%s" % (package, mesh_name)
    mesh = unreal.load_asset("%s/SkeletalMeshes/%s" % (base, mesh_name))
    if not mesh or not surface:
        continue
    slots = mesh.get_editor_property("materials")
    for index in range(len(slots)):
        slot = slots[index]  # a copy: it is written back below
        imported = slot.get_editor_property("material_interface")
        if not imported:
            continue
        name = str(imported.get_name())
        if name.startswith("MI_"):
            continue
        path = "%s/Materials/MI_%s" % (base, name)
        instance = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else asset_tools.create_asset(
            "MI_" + name, base + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        materials_lib.set_material_instance_parent(instance, surface)
        texture = unreal.load_asset("%s/Textures/%s" % (base, name))
        if texture:
            materials_lib.set_material_instance_texture_parameter_value(instance, "BaseColor", texture)
        # Suits and gear: matte cloth with a slight sheen on the brighter, worn parts.
        materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessDark", 0.8)
        materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessBright", 0.5)
        materials_lib.update_material_instance(instance)
        slot.set_editor_property("material_interface", instance)
        slots[index] = slot
    mesh.set_editor_property("materials", slots)
    # Setting the slots does not mark the mesh as changed, so it is saved explicitly.
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    log("materials set on %s: %d slots" % (mesh_name, len(slots)))

registry = unreal.AssetRegistryHelpers.get_asset_registry()
by_class = {}
for asset in registry.get_assets_by_path("/Game/Imported/Chars", recursive=True):
    by_class.setdefault(str(asset.asset_class_path.asset_name), []).append(str(asset.package_name))
for cls, names in sorted(by_class.items()):
    log("%s: %d e.g. %s" % (cls, len(names), names[:3]))
for path in by_class.get("SkeletalMesh", []):
    mesh = unreal.load_asset(path)
    skeleton = mesh.skeleton
    log("mesh %s bounds %s skeleton %s" % (path, mesh.get_bounds().box_extent, skeleton.get_path_name() if skeleton else None))
unreal.EditorAssetLibrary.save_directory("/Game/Imported/Chars", only_if_is_dirty=True, recursive=True)
