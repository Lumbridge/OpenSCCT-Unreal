"""Imports the outfit models maps ship (render/outfits.json, spec/svmap/1.2/outfits.md: a team's "model": {"glb": ...})
as skeletal meshes. Runs inside the editor's Python, with no window:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_outfits.py <content root> [<map> ...]" -nullrhi

Every map folder <content root>/svmap/<Map> (or only the maps named) whose render/outfits.json names a glb gets it
imported to /Game/Imported/Outfits/<Map>/<Name>/SkeletalMeshes/<Name>: the place the game looks for it
(UScctPawnLookSubsystem::OutfitModelPath in Source/OpenSCCT/Play/ScctPawnLookSubsystem.cpp), <Name> being the glb's
file name with anything but letters, digits and _ made _. Each material slot gets an instance of the project's
surface material with the slot's texture, as the stock characters do (import_characters.py). The bridge poses the
model's own skeleton with the team's stock clips. A model named by "stock" needs nothing here. Nothing is committed.
"""
import json
import os
import re
import sys

import unreal

ARGS = sys.argv[1:]
CONTENT_ROOT = ARGS[0]
ONLY = set(ARGS[1:])
DEST = "/Game/Imported/Outfits"
SURFACE_MATERIAL = "/Game/OpenSCCT/Materials/M_ScctSurface"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
materials_lib = unreal.MaterialEditingLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def key(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


def asset_name(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def outfit_models():
    """(map, glb path, asset name) of every map outfit glb."""
    found = []
    maps = os.path.join(CONTENT_ROOT, "svmap")
    for map_name in sorted(os.listdir(maps)) if os.path.isdir(maps) else []:
        if ONLY and map_name not in ONLY:
            continue
        doc = os.path.join(maps, map_name, "render", "outfits.json")
        if not os.path.isfile(doc):
            continue
        with open(doc, encoding="utf-8") as handle:
            outfits = json.load(handle)
        for team in ("spy", "merc"):
            model = (outfits.get(team) or {}).get("model") or {}
            glb = model.get("glb")
            if glb:
                path = os.path.join(maps, map_name, "render", *glb.split("/"))
                if os.path.isfile(path):
                    found.append((map_name, path, asset_name(os.path.splitext(os.path.basename(glb))[0])))
                else:
                    log("outfit model %s of %s is not there" % (glb, map_name))
    return found


def import_model(map_name, path, name):
    folder = "%s/%s" % (DEST, asset_name(map_name))
    wanted = "%s/%s/SkeletalMeshes/%s" % (folder, name, name)
    if not editor_assets.does_asset_exist(wanted):
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = folder
        task.automated = True
        task.save = True
        task.replace_existing = True
        asset_tools.import_asset_tasks([task])
    # The importer names the folder and mesh after the mesh inside the file: find what came out and name it after
    # the file, which is the name the game looks for.
    if not editor_assets.does_asset_exist(wanted):
        meshes = sorted(p.split(".")[0] for p in editor_assets.list_assets(folder, recursive=True)
                        if isinstance(unreal.load_asset(p.split(".")[0]), unreal.SkeletalMesh))
        if not meshes:
            log("NO SKELETAL MESH came out of %s" % path)
            return None
        if not editor_assets.rename_asset(meshes[0], wanted):
            log("could not rename %s to %s" % (meshes[0], wanted))
            return None
    return wanted


def set_materials(mesh_path, surface):
    mesh = unreal.load_asset(mesh_path)
    textures = {}
    folder = mesh_path.rsplit("/", 2)[0]
    for root in (folder, folder.rsplit("/", 1)[0]):
        for path in editor_assets.list_assets(root, recursive=True):
            asset = unreal.load_asset(path.split(".")[0])
            if isinstance(asset, unreal.Texture2D):
                textures.setdefault(key(path.rsplit("/", 1)[1].split(".")[0]), path.split(".")[0])
    slots = mesh.get_editor_property("materials")
    materials = folder + "/Materials"
    for index in range(len(slots)):
        slot = slots[index]
        imported = slot.get_editor_property("material_interface")
        name = str(imported.get_name()) if imported else "Slot%d" % index
        name = name[3:] if name.startswith("MI_") else name
        colour = textures.get(key(name)) or textures.get(key(name + "_baseColor"))
        instance_path = "%s/MI_%s" % (materials, name)
        instance = unreal.load_asset(instance_path) if editor_assets.does_asset_exist(instance_path) else asset_tools.create_asset(
            "MI_" + name, materials, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        materials_lib.set_material_instance_parent(instance, surface)
        if colour:
            materials_lib.set_material_instance_texture_parameter_value(instance, "BaseColor", unreal.load_asset(colour))
        else:
            log("no texture for material %s of %s (have %s)" % (name, mesh_path, sorted(textures)))
        for parameter, value in (("RoughnessDark", 0.8), ("RoughnessBright", 0.5), ("Metallic", 0.0), ("Emissive", 0.0)):
            materials_lib.set_material_instance_scalar_parameter_value(instance, parameter, value)
        materials_lib.update_material_instance(instance)
        editor_assets.save_loaded_asset(instance, only_if_is_dirty=False)
        slot.set_editor_property("material_interface", instance)
        slots[index] = slot
    mesh.set_editor_property("materials", slots)
    editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)
    bones = len(mesh.skeleton.get_reference_pose().get_bone_names()) if mesh.skeleton else 0
    log("outfit model %s: %d slots, %d bones" % (mesh_path, len(slots), bones))


surface = unreal.load_asset(SURFACE_MATERIAL)
if not surface:
    raise RuntimeError("no %s: run Tools/Import/Import-Assets.ps1 -MaterialsOnly first" % SURFACE_MATERIAL)
models = outfit_models()
for map_name, glb, name in models:
    imported = import_model(map_name, glb, name)
    if imported:
        set_materials(imported, surface)
log("outfit models: %d" % len(models))
editor_assets.save_directory(DEST, only_if_is_dirty=True, recursive=True)
