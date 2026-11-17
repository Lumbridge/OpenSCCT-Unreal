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
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/Game/Imported/Chars/%s" % package
    task.automated = True
    task.save = True
    task.replace_existing = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

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
