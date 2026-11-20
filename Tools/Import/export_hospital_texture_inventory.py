"""Export the texture sources and effective material assignments of the imported Hospital."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import shutil

import unreal


MAP = "/Game/Imported/Maps/MounE"
LEVEL = MAP + "/MounE"
MATERIAL = unreal.MaterialEditingLibrary
MATERIAL_CLASSES = ("MaterialInstanceConstant", "Material")
STAIR_NAMES = (
    MAP + "/Materials/MI_Prison_TEX_FLO_concrete01",
    MAP + "/Materials/MI_Prison_TEX_FLO_concrete01_2",
    MAP + "/world/Materials/Prison_TEX_FLO_concrete01",
    MAP + "/world/Materials/Prison_TEX_FLO_concrete01_2",
)


def package(asset):
    return asset.get_path_name().split(".", 1)[0]


def game_references(registry):
    options = unreal.AssetRegistryDependencyOptions()
    options.include_soft_package_references = False
    options.include_editor_only_package_references = False
    incoming = defaultdict(set)
    pending = [LEVEL]
    seen = set()
    while pending:
        source = pending.pop()
        if source in seen:
            continue
        seen.add(source)
        for name in registry.get_dependencies(source, options) or []:
            target = str(name)
            incoming[target].add(source)
            if target.startswith(MAP + "/"):
                pending.append(target)
    return incoming


def mesh_slots(entries, incoming):
    slots = defaultdict(list)
    empty = []
    for name, entry in sorted(entries.items()):
        if str(entry.asset_class_path.asset_name) != "StaticMesh" or name not in incoming:
            continue
        mesh = entry.get_asset()
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            label = str(slot.get_editor_property("material_slot_name"))
            material = slot.get_editor_property("material_interface")
            slot_report = {"mesh": name, "slot": index, "slot_name": label,
                           "imported_slot_name": str(slot.get_editor_property("imported_material_slot_name"))}
            if material:
                slots[material.get_path_name()].append((material, slot_report))
            else:
                empty.append(slot_report)
            overlay = slot.get_editor_property("overlay_material_interface")
            if overlay:
                slots[overlay.get_path_name()].append((overlay, dict(slot_report, overlay=True)))
    return slots, empty


def parameter_owners(material):
    owners = {}
    parent_chain = []
    while isinstance(material, unreal.MaterialInstanceConstant):
        parent_chain.append(material.get_path_name())
        for value in material.get_editor_property("texture_parameter_values"):
            info = value.get_editor_property("parameter_info")
            name = str(info.get_editor_property("name"))
            if info.get_editor_property("association") == unreal.MaterialParameterAssociation.GLOBAL_PARAMETER:
                if value.get_editor_property("parameter_value"):
                    owners.setdefault(name.lower(), material.get_path_name())
        material = material.get_editor_property("parent")
    if material:
        parent_chain.append(material.get_path_name())
    return owners, parent_chain


def report_material(material, slots, incoming, textures):
    owners, chain = parameter_owners(material)
    shader_textures = {item.get_path_name(): item for item in MATERIAL.get_material_used_textures(material) if item}
    bindings = {}
    for raw in MATERIAL.get_texture_parameter_names(material):
        name = str(raw)
        if isinstance(material, unreal.MaterialInstanceConstant):
            texture = MATERIAL.get_material_instance_texture_parameter_value(material, name)
        else:
            texture = MATERIAL.get_material_default_texture_parameter_value(material, name)
        if not texture:
            continue
        path = texture.get_path_name()
        textures[path] = texture
        bindings[name] = {
            "texture": path,
            "owner": owners.get(name.lower(), chain[-1] if chain else None),
            "shader_reports_texture": path in shader_textures if shader_textures else None,
        }
    bound = {row["texture"] for row in bindings.values()}
    textures.update(shader_textures)
    return {
        "parent_chain": chain,
        "game_package_referencers": sorted(incoming.get(package(material), [])),
        "static_mesh_slots": [row for _, row in slots],
        "bindings": bindings,
        "shader_textures_outside_named_slots": sorted(shader_textures.keys() - bound),
    }


def source_paths(texture):
    try:
        data = texture.get_editor_property("asset_import_data")
    except Exception:
        return []
    return [str(path) for path in data.extract_filenames()] if data else []


def run_export(texture, image, output):
    image.parent.mkdir(parents=True, exist_ok=True)
    temporary = image.with_name(image.stem + ".exporting" + image.suffix)
    temporary.unlink(missing_ok=True)
    task = unreal.AssetExportTask()
    task.object = texture
    task.filename = str(temporary)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if unreal.Exporter.run_asset_export_task(task) and temporary.is_file():
        temporary.replace(image)
        return image.relative_to(output).as_posix()
    temporary.unlink(missing_ok=True)
    return None


def export_image(texture, output, filenames, metadata):
    path = texture.get_path_name()
    if not path.startswith(("/Game/", "/Engine/")):
        return None, "Transient or native texture"
    name = output / "images" / path.split(".", 1)[0].lstrip("/")
    if isinstance(texture, unreal.Texture2D):
        # Export the texture's editor source instead of a potentially stale import filename.
        # Engine placeholders without source data are retained in the report without an image.
        if path.startswith("/Engine/") and not filenames:
            return None, "Engine default texture"
        compression = metadata["compression"]
        preferred = (".exr", ".png") if "HDR" in compression else (".png", ".exr")
        for extension in preferred:
            image = run_export(texture, name.with_suffix(extension), output)
            if image:
                return image, "Unreal source pixels"
        return None, "Source export unavailable"

    available = [Path(file) for file in filenames if Path(file).is_file()]
    if len(available) == 1:
        source = available[0]
        image = name.with_suffix(source.suffix)
        image.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, image)
        return image.relative_to(output).as_posix(), "Imported source file for non-2D texture"
    if isinstance(texture, unreal.TextureCube):
        image = run_export(texture, name.with_suffix(".hdr"), output)
        if image:
            return image, "Unreal cubemap HDR export"
    return None, "No single source image export for texture class"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([MAP])
    entries = {str(entry.package_name): entry for entry in registry.get_assets_by_path(MAP, recursive=True)}
    if LEVEL not in entries:
        raise ValueError("The imported Enhanced Mount Hospital level was not found")
    incoming = game_references(registry)
    if not incoming:
        raise RuntimeError("The asset registry returned no game dependencies for the Hospital level")
    slots, empty = mesh_slots(entries, incoming)
    materials = {path: rows[0][0] for path, rows in slots.items()}
    for name, entry in sorted(entries.items()):
        if name in incoming and str(entry.asset_class_path.asset_name) in MATERIAL_CLASSES:
            material = entry.get_asset()
            if material:
                materials[material.get_path_name()] = material

    textures = {}
    report = {path: report_material(material, slots.get(path, []), incoming, textures)
              for path, material in sorted(materials.items())}
    material_packages = {package(material) for material in materials.values()}
    # A light or actor can hold a texture directly, outside of a material parameter.
    for name, referencers in sorted(incoming.items()):
        if not name.startswith("/Game/") or not referencers - material_packages:
            continue
        for entry in registry.get_assets_by_package_name(name) or []:
            if str(entry.asset_class_path.asset_name).startswith("Texture"):
                texture = entry.get_asset()
                if isinstance(texture, unreal.Texture):
                    textures.setdefault(texture.get_path_name(), texture)
    textures_report = {}
    for path, texture in sorted(textures.items()):
        filenames = source_paths(texture)
        row = {"class": texture.get_class().get_name(), "import_filenames": filenames,
               "srgb": texture.get_editor_property("srgb"),
               "compression": str(texture.get_editor_property("compression_settings")),
               "game_package_referencers": sorted(incoming.get(package(texture), []))}
        if isinstance(texture, unreal.Texture2D):
            row["size"] = [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]
        row["image"], row["export"] = (None, "Manifest only") if args.manifest_only else export_image(texture, output, filenames, row)
        textures_report[path] = row
    result = {
        "map": LEVEL,
        "scope": "Saved-level hard game dependencies and effective imported static mesh defaults. Runtime-only substitutes and UI are outside this report.",
        "materials": report,
        "textures": textures_report,
        "empty_static_mesh_slots": empty,
        "stair_aliases": {name: {"exists": name in entries, "game_package_referencers": sorted(incoming.get(name, []))}
                         for name in STAIR_NAMES},
    }
    manifest = output / "hospital-textures.json"
    temporary = manifest.with_suffix(".tmp")
    temporary.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    temporary.replace(manifest)
    exported = sum(bool(row["image"]) for row in textures_report.values())
    unreal.log_warning(f"SCCT-IMPORT Audited {len(report)} Hospital materials and {len(textures_report)} texture assets, "
                       f"exported {exported} source images to {output}")


if __name__ == "__main__":
    main()
