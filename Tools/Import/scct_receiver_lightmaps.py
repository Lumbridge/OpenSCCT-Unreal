"""Prepare native overlap correction for BSP UV1 charts in an explicitly prepared Unreal map."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import unreal

ASSETS = unreal.EditorAssetLibrary
MATERIAL = unreal.MaterialEditingLibrary
SOURCE_TAG = "ScctResidualSource"


def fixture_list(world, sources):
    fixtures = []
    by_name = {row["name"].casefold(): row for row in sources}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Light):
        tags = {str(tag) for tag in actor.tags}
        if not tags.intersection({"ScctLumenFixture", "ScctRecoveredFixture", "ScctTankFixture"}):
            continue
        src = next((by_name[tag[5:].casefold()] for tag in sorted(tags)
                    if tag.startswith("Scct:") and tag[5:].casefold() in by_name), None)
        # Only continuously lit points and ordinary cones have a known static footprint.
        # Patterned projectors and switchable lamps retain their authored indirect field.
        if (not src or not src.get("initially_on", True) or src.get("ltype", 1) != 1
                or src.get("effect", 0) not in (0, 8, 12) or any(src.get(key) for key in
                ("hidden", "triggered", "dynamic", "gameplay", "special_lit"))):
            continue
        component = actor.light_component
        if not isinstance(component, unreal.PointLightComponent):
            continue
        position = component.get_world_location()
        source_luma = sum(w * max(0., c)**2.2 for w, c in zip((.2126, .7152, .0722), src["color"]))
        lamp = dict(name=src["name"], position=[position.x, position.y, position.z],
                    radius=component.attenuation_radius, strength=(src["brightness"]/255.)**2.2
                    * source_luma * max(0., min(float(component.diffuse_scale), 1.)))
        if isinstance(component, unreal.SpotLightComponent):
            v = component.get_forward_vector()
            # At match start ScctLightRules narrows the imported cone's inner angle.
            outer = min(max(float(component.outer_cone_angle), 1.), 80.)
            lamp.update(direction=[v.x, v.y, v.z], inner=outer * .05, outer=outer)
        if src.get("own_zone_only"):
            lamp["zone"] = src.get("zone") or ""
        if lamp["strength"] > 0 and lamp["radius"] > 0:
            fixtures.append(lamp)
    return sorted(fixtures, key=lambda row: row["name"].casefold())


def base_page(material):
    texture = MATERIAL.get_material_instance_texture_parameter_value(material, "Lightmap")
    if texture:
        original = ASSETS.get_metadata_tag(texture, SOURCE_TAG)
        if original:
            texture = unreal.load_asset(str(original))
            if not texture:
                raise RuntimeError(f"{material.get_path_name()}: missing original baked atlas {original}")
    return texture


def import_page(filename, folder, name, source):
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    image = unreal.load_asset(folder + "/" + name)
    if not isinstance(image, unreal.Texture2D):
        raise RuntimeError(f"Failed to import {filename}")
    for prop in ("srgb", "compression_settings", "lod_group", "mip_gen_settings", "lod_bias",
                 "max_texture_size", "filter", "address_x", "address_y", "never_stream"):
        image.set_editor_property(prop, source.get_editor_property(prop))
    ASSETS.set_metadata_tag(image, SOURCE_TAG, source.get_path_name())
    if not ASSETS.save_loaded_asset(image, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {image.get_path_name()}")
    return image


def prepare(world, source_lights, map_folder):
    map_folder = Path(map_folder)
    key = map_folder.name
    root = f"/Game/Imported/Maps/{key}"
    render = map_folder / "render"
    definitions = json.loads((render / "materials.json").read_text(encoding="utf-8"))
    def material_key(name):
        return re.sub(r"[^a-z0-9_]", "_", name.casefold())

    flags = {material_key(name): row["flags"] for name, row in definitions["materials"].items()}
    pages = {}
    for entry in definitions["lightmapped"]:
        name = material_key(entry["name"])
        traits = {**entry, **flags.get(name, {})}
        if not any(traits.get(k) for k in ("translucent", "masked", "unlit", "two_sided", "self_illum", "additive")):
            pages[name] = entry["lightmap"]
    folder = root + "/NativeLighting/Residual"
    work = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "ReceiverLightmaps" / key
    work.mkdir(parents=True, exist_ok=True)
    # Bind only named world lightmapped instances. Standalone character/prop materials,
    # additive cards, texture alpha and mesh vertex colours do not share these overrides.
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    by_texture = {}
    for asset in registry.get_assets_by_path(root + "/Materials", recursive=False):
        asset_name = str(asset.asset_name).casefold()
        page = pages.get(asset_name[3:]) if asset_name.startswith("mi_") else None
        if not page or str(asset.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        material = asset.get_asset()
        source = base_page(material)
        if not isinstance(source, unreal.Texture2D):
            continue
        if not source.get_path_name().startswith(root + "/"):
            continue
        record = by_texture.setdefault(source.get_path_name(), (source, page, []))
        if record[1] != page:
            raise RuntimeError(f"{source.get_name()} maps to multiple source charts")
        record[2].append(material)
    if not by_texture:
        unreal.log_warning(f"SCCT-RESIDUAL {key}: no bound original BSP atlases")
        return
    fixtures = fixture_list(world, source_lights)
    textures = []
    for source, page, materials in sorted(by_texture.values(), key=lambda item: item[0].get_path_name()):
        name = source.get_name()
        original = work / (name + "_Source.png")
        task = unreal.AssetExportTask()
        task.object = source
        task.filename = str(original)
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        if not unreal.Exporter.run_asset_export_task(task) or not original.is_file():
            raise RuntimeError(f"Could not export the clean source {source.get_path_name()}")
        textures.append(dict(page=page, source=str(original), output=str(work/(name + "_Residual.png"))))
    job = work / "job.json"
    job.write_text(json.dumps(dict(render=str(render), fixtures=fixtures, textures=textures, receivers=sorted(pages)), indent=2), encoding="utf-8")
    python = os.environ.get("SCCT_TOOLS_PYTHON") or shutil.which("python")
    if not python:
        raise RuntimeError("Set SCCT_TOOLS_PYTHON to the external Python with NumPy and Pillow")
    tool = Path(__file__).with_name("prepare_receiver_lightmaps.py")
    run = subprocess.run([python, str(tool), str(job)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if run.returncode:
        raise RuntimeError("Receiver lightmap preparation failed: " + run.stdout[-3000:])
    unreal.log_warning("SCCT-RESIDUAL " + run.stdout.strip())
    for (source, _, materials), record in zip(sorted(by_texture.values(), key=lambda item: item[0].get_path_name()), textures):
        path = Path(record["output"])
        residual = import_page(path, folder, path.stem, source)
        for material in materials:
            if MATERIAL.get_material_instance_texture_parameter_value(material, "Lightmap") == residual:
                continue
            MATERIAL.set_material_instance_texture_parameter_value(material, "Lightmap", residual)
            if (MATERIAL.get_material_instance_texture_parameter_value(material, "Lightmap") != residual
                    or not ASSETS.save_loaded_asset(material, only_if_is_dirty=False)):
                raise RuntimeError(f"Could not bind {residual.get_name()} on {material.get_name()}")
        unreal.log_warning(f"SCCT-RESIDUAL {residual.get_name()}: {len(materials)} verified BSP bindings, {len(fixtures)} permanent fixtures")
