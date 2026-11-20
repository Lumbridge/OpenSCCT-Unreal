"""Bind refined baked atlases to the existing Enhanced Mount Hospital Lightmap parameters."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct

import unreal


MAP = "/Game/Imported/Maps/MounE"
SOURCE = MAP + "/world/Textures/"
DESTINATION = MAP + "/HospitalLightmaps"
PAGES = tuple(f"textures/MounE_lightmap{index}.png" for index in range(3))
ASSETS = unreal.EditorAssetLibrary
MATERIAL = unreal.MaterialEditingLibrary


def log(message):
    unreal.log_warning("SCCT-IMPORT " + message)


def refined_name(page):
    return Path(page).stem + "_Refined"


def object_path(folder, name):
    return f"{folder.rstrip('/')}/{name}.{name}"


def path_of(asset):
    return asset.get_path_name() if asset else ""


def local_lightmap(instance):
    for override in instance.get_editor_property("texture_parameter_values"):
        info = override.get_editor_property("parameter_info")
        if (str(info.get_editor_property("name")).lower() == "lightmap"
                and info.get_editor_property("association") == unreal.MaterialParameterAssociation.GLOBAL_PARAMETER):
            return override.get_editor_property("parameter_value")
    return None


def lightmap(instance):
    return MATERIAL.get_material_instance_texture_parameter_value(instance, "Lightmap")


def size_of(texture):
    return texture.blueprint_get_size_x(), texture.blueprint_get_size_y()


def check_png(path, stock):
    with path.open("rb") as png:
        header = png.read(26)
    if len(header) != 26 or header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR":
        raise ValueError(f"{path}: expected a PNG atlas")
    width, height, bits, color = struct.unpack(">IIBB", header[16:])
    if bits != 8 or color not in (2, 6):
        raise ValueError(f"{path}: expected an 8-bit RGB or RGBA atlas")
    sw, sh = size_of(stock)
    if sw <= 0 or sh <= 0 or width < sw or width % sw or height != sh * (width // sw):
        raise ValueError(f"{path}: expected an even pixel scale of the original {sw}x{sh} atlas")
    return width, height


def read_manifest(path, originals):
    plan = json.loads(path.read_text(encoding="utf-8"))
    if plan["map"] != "MounE" or set(plan["pages"]) != set(PAGES):
        raise ValueError("Expected the three Enhanced Mount Hospital baked lightmap pages")

    images = {}
    for page in PAGES:
        file = plan["pages"][page]["file"]
        if file != refined_name(page) + ".png":
            raise ValueError(f"{page}: unexpected atlas filename {file}")
        png = path.parent / file
        images[page] = (png, check_png(png, originals[page]))

    expected = {}
    for converted, page in plan["materials"].items():
        name = "MI_" + re.sub(r"[^A-Za-z0-9_]", "_", converted)
        if name.lower() in expected or page not in images:
            raise ValueError(f"{converted}: duplicate material name or unknown atlas")
        expected[name.lower()] = page
    if set(expected.values()) != set(PAGES):
        raise ValueError("Missing converted material assignments for one or more atlas pages")
    return images, expected


def import_atlases(images, originals):
    tasks = []
    for page, (path, _) in images.items():
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = DESTINATION
        task.automated = True
        task.save = False
        task.replace_existing = True
        task.replace_existing_settings = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    atlases = {}
    for (page, (_, size)), task in zip(images.items(), tasks):
        expected = object_path(DESTINATION, refined_name(page))
        texture = next((asset for asset in task.get_objects() if path_of(asset) == expected), None)
        stock = originals[page]
        if not isinstance(texture, unreal.Texture2D) or size_of(texture) != size:
            raise ValueError(f"{page}: the refined texture was not imported at {size}")
        for prop in ("address_x", "address_y", "filter", "lod_group", "lod_bias", "never_stream",
                     "mip_gen_settings", "preserve_border"):
            texture.set_editor_property(prop, stock.get_editor_property(prop))
        # Legacy page bytes are display values. Match the original shader's sRGB color sampler.
        texture.set_editor_property("srgb", True)
        # UE's uncompressed RGBA8 setting builds as BGRA8 on Windows, including at the smaller mips.
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("virtual_texture_streaming", False)
        if not ASSETS.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"{page}: could not save refined texture")
        atlases[page] = texture
        log(f"{texture.get_name()}: {size[0]}x{size[1]}, sRGB BGRA8, {texture.get_editor_property('mip_gen_settings')}")
    return atlases


def material_bindings(existing_pages):
    effective = []
    direct = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for asset in registry.get_assets_by_path(MAP, recursive=True):
        if str(asset.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        instance = asset.get_asset()
        page = existing_pages.get(path_of(lightmap(instance)))
        if page is None:
            continue
        effective.append((instance, page))
        if existing_pages.get(path_of(local_lightmap(instance))) == page:
            direct.append((instance, page))
    return effective, direct


def update_materials(effective, direct, textures):
    for instance, page in direct:
        texture = textures[page]
        if local_lightmap(instance) == texture:
            continue
        # In UE 5.8 this setter applies successfully but always returns false.
        MATERIAL.set_material_instance_texture_parameter_value(instance, "Lightmap", texture)
        if local_lightmap(instance) != texture or not ASSETS.save_loaded_asset(instance, only_if_is_dirty=False):
            raise RuntimeError(f"{instance.get_name()}: could not save Lightmap binding")

    mismatched = [str(instance.get_name()) for instance, page in effective if lightmap(instance) != textures[page]]
    if mismatched:
        raise RuntimeError("Effective Lightmap bindings did not switch: " + ", ".join(mismatched))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--manifest", type=Path)
    operation.add_argument("--restore", action="store_true")
    args = parser.parse_args()

    originals = {page: unreal.load_asset(object_path(SOURCE, Path(page).stem)) for page in PAGES}
    if not all(isinstance(texture, unreal.Texture2D) and texture.get_editor_property("srgb")
               for texture in originals.values()):
        raise ValueError("The imported map must have its three original sRGB atlas textures")

    known = {object_path(DESTINATION, refined_name(page)): page for page in PAGES}
    if args.restore:
        effective, direct = material_bindings(known)
        update_materials(effective, direct, originals)
        log(f"Restored {len(effective)} Lightmap bindings ({len(direct)} direct overrides)")
        return

    images, expected = read_manifest(args.manifest, originals)
    known.update((path_of(texture), page) for page, texture in originals.items())
    effective, direct = material_bindings(known)
    observed = {str(instance.get_name()).lower(): page for instance, page in effective}
    # The converted GLB records this water face in the atlas. Its imported water shader samples no baked page.
    water_name = "MI_Prison_TEX_water__lm2"
    water = unreal.load_asset(object_path(MAP + "/Materials", water_name))
    if (isinstance(water, unreal.MaterialInstanceConstant)
            and path_of(water.get_editor_property("parent")) == "/Game/OpenSCCT/Materials/M_ScctWater.M_ScctWater"
            and not lightmap(water)):
        expected.pop(water_name.lower(), None)
        log(f"{water_name} uses the live water shader without a Lightmap input")
    mismatched = sorted(name for name, page in expected.items() if observed.get(name) != page)
    if mismatched:
        raise ValueError("Converted materials missing their expected Lightmap bindings: " + ", ".join(mismatched))

    refined = import_atlases(images, originals)
    update_materials(effective, direct, refined)
    counts = Counter(page for _, page in effective)
    for page in PAGES:
        log(f"{page}: {counts[page]} verified effective bindings")
    log(f"Applied {len(refined)} atlases to {len(effective)} Lightmap bindings ({len(direct)} direct overrides)")


if __name__ == "__main__":
    main()
