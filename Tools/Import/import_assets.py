"""Imports the packed CC0 assets and builds the master materials. Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_assets.py <packed folder> [materials]"

<packed folder> is what fetch_assets.py wrote. The textures become /Game/OpenSCCT/ThirdParty/<source>/ (committed:
they are CC0, see docs/ASSETS.md); textures already imported are left alone. With the word "materials" as the
second argument only the master materials are rebuilt (after a change to scct_materials.py).
"""
import json
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scct_materials  # noqa: E402  pylint: disable=wrong-import-position

PACKED = sys.argv[1]
MATERIALS_ONLY = len(sys.argv) > 2 and sys.argv[2] == "materials"
FOLDERS = {"ambientCG": scct_materials.THIRD_PARTY + "/ambientCG", "Poly Haven": scct_materials.THIRD_PARTY + "/PolyHaven"}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def import_file(path, destination):
    """Imports one file unless its asset is already there. Returns the asset path."""
    asset = destination + "/" + os.path.splitext(os.path.basename(path))[0]
    if not editor_assets.does_asset_exist(asset):
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = destination
        task.automated = True
        task.save = True
        asset_tools.import_asset_tasks([task])
    return asset


def set_up(asset, **properties):
    texture = unreal.load_asset(asset)
    if texture is None:
        log("NOT IMPORTED: " + asset)
        return
    changed = False
    for name, value in properties.items():
        if texture.get_editor_property(name) != value:
            texture.set_editor_property(name, value)
            changed = True
    if changed:
        editor_assets.save_asset(asset)


def main():
    catalogue = json.load(open(os.path.join(HERE, "pbr_sets.json")))
    if not MATERIALS_ONLY:
        for name, record in catalogue["sets"].items():
            folder = FOLDERS[record["source"]]
            normal = import_file(os.path.join(PACKED, "T_%s_N.jpg" % name), folder)
            set_up(normal, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP)
            packed = import_file(os.path.join(PACKED, "T_%s_ORD.jpg" % name), folder)
            # Three unrelated grey channels: linear, and compressed without mixing them.
            set_up(packed, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_MASKS)
        for name, record in catalogue["hdris"].items():
            folder = FOLDERS[record["source"]]
            # The full panorama is what you see; the small copy lights the level (a small cube map filters cleanly).
            import_file(os.path.join(PACKED, name + ".hdr"), folder)
            import_file(os.path.join(PACKED, name + "_light.hdr"), folder)
        ripples = import_file(os.path.join(PACKED, "T_WaterNormal.png"), scct_materials.MATERIALS)
        set_up(ripples, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP)
        log("third-party textures ready")
    scct_materials.build_all()
    log("master materials ready")


main()
