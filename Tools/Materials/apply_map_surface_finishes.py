"""Apply the mercenary camera surface finishes to an existing import without disturbing local colour restorations.

Run in editor Python with the map key and its original render/materials.json as arguments.
"""
import json
from pathlib import Path
import re
import sys

import unreal

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "Tools/Import"))
import scct_surface_finish


def ensure_finish_textures(materials):
    destination = "/Game/OpenSCCT/Materials/Finishes"
    sources = {record.get("source") or name for name, record in materials.items()}
    for name in sorted({atlas for source, (_, atlas) in scct_surface_finish.SOURCES.items() if source in sources and atlas}):
        if unreal.EditorAssetLibrary.does_asset_exist(destination + "/" + name):
            continue
        task = unreal.AssetImportTask()
        task.filename = str(root / "Art/Maps/MounE" / (name + ".png"))
        task.destination_path = destination
        task.automated = True
        task.replace_existing = True
        task.save = False
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.load_asset(destination + "/" + name)
        if not isinstance(tex, unreal.Texture2D) or not task.imported_object_paths:
            raise RuntimeError(f"Could not import {name}")
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        if not unreal.EditorAssetLibrary.save_loaded_asset(tex, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {name}")


def main(map_key, manifest):
    materials = json.loads(Path(manifest).read_text(encoding="utf-8-sig"))["materials"]
    ensure_finish_textures(materials)
    catalogue = json.loads((root / "Tools/Import/surface_classes.json").read_text())
    classes = {**catalogue["materials"], **catalogue["overrides"]}
    material_root = f"/Game/Imported/Maps/{map_key}/Materials/MI_"
    surface = unreal.load_asset("/Game/OpenSCCT/Materials/M_ScctSurface")
    processed, saved = 0, 0
    for name, material in materials.items():
        flags = material["flags"]
        if flags.get("unlit") or flags.get("translucent") or "authored" in flags:
            continue
        path = material_root + re.sub(r"[^A-Za-z0-9_]", "_", name)
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            raise RuntimeError(f"Imported material missing: {path}")
        instance = unreal.load_asset(path)
        if instance.parent != surface:
            continue
        source = material.get("source") or name
        class_name = classes.get(source, catalogue["fallback"])
        look = catalogue["classes"][class_name]
        if look["kind"] != "surface" and source not in scct_surface_finish.SOURCES:
            continue
        processed += 1
        if scct_surface_finish.bind(instance, source, class_name, look):
            unreal.MaterialEditingLibrary.update_material_instance(instance)
            if not unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False):
                raise RuntimeError(f"Could not save {path}")
            saved += 1
    unreal.log_warning(f"SCCT-IMPORT {map_key} physical surface finishes: {processed} eligible instances, {saved} updated")


if __name__ == "__main__":
    main(*sys.argv[1:])
