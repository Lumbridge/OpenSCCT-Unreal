"""Bind uncompressed source textures to Mount Hospital's sprite and corona materials."""
import argparse
from pathlib import Path

import unreal

from apply_hospital_lightmaps import MAP, ASSETS, MATERIAL, check_png, log, object_path, path_of, size_of


DESTINATION = MAP + "/HospitalEffectTextures"
EFFECTS = {
    "MHOsfx_xbox_butterfly": "MI_Fx_MHOsfx_xbox_butterfly_glow_3x3",
    "MHOsfx_xbox_fumee_tuyau_light": "MI_Fx_MHOsfx_xbox_fumee_tuyau_light_lit_1x1",
    "Prison_TEX_Corona": "MI_Corona_Prison_TEX_Corona",
}


def binding(instance):
    return MATERIAL.get_material_instance_texture_parameter_value(instance, "Texture")


def import_textures(source, originals):
    tasks = {}
    for name, stock in originals.items():
        path = source / (name + ".png")
        if check_png(path, stock) != size_of(stock):
            raise ValueError(f"{path}: expected the original sprite dimensions")
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = DESTINATION
        task.automated = True
        task.save = False
        task.replace_existing = True
        task.replace_existing_settings = True
        tasks[name] = task
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(list(tasks.values()))

    textures = {}
    for name, task in tasks.items():
        expected = object_path(DESTINATION, name)
        texture = next((asset for asset in task.get_objects() if path_of(asset) == expected), None)
        stock = originals[name]
        if not isinstance(texture, unreal.Texture2D) or size_of(texture) != size_of(stock):
            raise ValueError(f"{name}: effect texture import failed")
        for prop in ("srgb", "address_x", "address_y", "filter", "lod_group", "lod_bias", "never_stream",
                     "mip_gen_settings", "preserve_border", "virtual_texture_streaming"):
            texture.set_editor_property(prop, stock.get_editor_property(prop))
        # Reuse the exact RGBA input, without introducing another block-compression pass.
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("compression_no_alpha", False)
        if not ASSETS.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"{name}: could not save uncompressed effect texture")
        textures[name] = texture
    return textures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--textures", type=Path)
    operation.add_argument("--restore", action="store_true")
    args = parser.parse_args()

    originals = {}
    instances = {}
    for name, material in EFFECTS.items():
        instance = unreal.load_asset(object_path(MAP + "/Effects", material))
        stock = unreal.load_asset(object_path(MAP + "/Extra", name))
        if not isinstance(instance, unreal.MaterialInstanceConstant) or not isinstance(stock, unreal.Texture2D):
            raise ValueError(f"{name}: missing the imported effect material or its source texture")
        if path_of(binding(instance)) not in (path_of(stock), object_path(DESTINATION, name)):
            raise ValueError(f"{material}: unexpected effect texture binding")
        originals[name] = stock
        instances[name] = instance

    textures = originals if args.restore else import_textures(args.textures, originals)
    for name, instance in instances.items():
        texture = textures[name]
        if binding(instance) != texture:
            MATERIAL.set_material_instance_texture_parameter_value(instance, "Texture", texture)
            if binding(instance) != texture or not ASSETS.save_loaded_asset(instance, only_if_is_dirty=False):
                raise RuntimeError(f"{instance.get_name()}: could not save effect texture binding")
        log(f"{instance.get_name()}: {path_of(texture)}")


if __name__ == "__main__":
    main()
