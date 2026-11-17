"""Imports the converted character models (spy and merc) and everything they hold and wear as skeletal meshes.
Runs inside the editor's Python, with no window:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_characters.py <content root>" -nullrhi

What is imported:
  - the bodies SPerso.ATT_01 (spy) and SPerso.DEF_01 (merc), with their animations;
  - the equipment the bridge's catalog lists (Bridge/OpenScct.UnrealBridge/Equipment/EquipmentCatalog.cs): every
    .glb of shared/chars/SWeaponFirst, SGoggle, OpenScctSidearms and OpenScctRifles, and the spy's eye-view arms
    SPerso.ATT_01_fp. These come without their animations: the bridge computes every pose itself.
  - the goggle lens textures of shared/chars/SGoggle/textures, each with a lens material (the simulation names
    the lens colour per vision mode by texture).

Every file <Package>/<Mesh>.glb becomes /Game/Imported/Chars/<Package>/<Mesh>/SkeletalMeshes/<Mesh> (the name the
game looks for, whatever the mesh is called inside the file), with the original skeleton: the simulation's
poses are computed for it. Nothing here is committed.
"""
import os
import re
import sys

import unreal

CONTENT_ROOT = sys.argv[1]
CHARS = os.path.join(CONTENT_ROOT, "shared", "chars")
DEST = "/Game/Imported/Chars"
BODIES = [("SPerso", "ATT_01"), ("SPerso", "DEF_01")]

# The same rule as the bridge's catalog: these packages hold equipment, plus the spy's eye-view arms.
EQUIPMENT_PACKAGES = ["SWeaponFirst", "SGoggle", "OpenScctSidearms", "OpenScctRifles"]
EQUIPMENT_EXTRA = [("SPerso", "ATT_01_fp")]

SURFACE_MATERIAL = "/Game/OpenSCCT/Materials/M_ScctSurface"
GLOW_MATERIAL = "/Game/OpenSCCT/Weapons/Materials/M_ScctGoggleGlow"  # Tools/Import/build_equipment_assets.py

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
materials_lib = unreal.MaterialEditingLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def key(name):
    """A name reduced to letters and digits: the importer replaces punctuation in its own ways."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


def mesh_path(package, mesh):
    return "%s/%s/%s/SkeletalMeshes/%s" % (DEST, package, mesh, mesh)


def equipment_models():
    models = []
    for package in EQUIPMENT_PACKAGES:
        folder = os.path.join(CHARS, package)
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder), key=str.lower):
            if name.lower().endswith(".glb"):
                models.append((package, name[:-4]))
    return models + EQUIPMENT_EXTRA


def pipelines_without_animations():
    """The importer's own glTF pipelines with the animation import switched off, for this run only."""
    assets = unreal.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline")
    if not assets:
        return None
    try:
        assets.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    except Exception as error:  # pylint: disable=broad-except
        log("animations stay on: %s" % error)
    override = unreal.InterchangePipelineStackOverride()
    override.set_editor_property("override_pipelines", [
        unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFAssetsPipeline.DefaultGLTFAssetsPipeline"),
        unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"),
    ])
    return override


def import_models(models, options):
    tasks = []
    for package, mesh in models:
        path = os.path.join(CHARS, package, mesh + ".glb")
        if not os.path.exists(path):
            log("missing " + path)
            continue
        if editor_assets.does_asset_exist(mesh_path(package, mesh)):
            continue  # already imported; only the materials below are refreshed
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = "%s/%s" % (DEST, package)
        task.automated = True
        task.save = True
        task.replace_existing = True
        if options:
            task.options = options
        tasks.append(task)
    if tasks:
        asset_tools.import_asset_tasks(tasks)


def skeletal_meshes_of(package, mesh):
    """Every skeletal mesh that came out of one file, the one named after the file first."""
    folder = "%s/%s/%s" % (DEST, package, mesh)
    found = sorted(path.split(".")[0] for path in editor_assets.list_assets(folder, recursive=True)
                   if isinstance(unreal.load_asset(path.split(".")[0]), unreal.SkeletalMesh))
    wanted = mesh_path(package, mesh)
    return ([wanted] if wanted in found else []) + [path for path in found if path != wanted]


def name_mesh_after_file(package, mesh):
    """The importer names a skeletal mesh after the mesh inside the file (p190_fp.glb holds "p190"). The game
    looks for the file's name, so the skinned mesh is renamed. The first-person pistols of OpenScctSidearms hold
    several meshes on one skeleton (arms, gun, dropped magazine): one of them takes the file's name and the game
    draws the others of the folder with it."""
    wanted = mesh_path(package, mesh)
    if editor_assets.does_asset_exist(wanted):
        return True
    found = skeletal_meshes_of(package, mesh)
    if not found:
        log("NO SKELETAL MESH came out of %s/%s.glb" % (package, mesh))
        return False
    if len(found) > 1:
        log("%s/%s.glb gave %d skeletal meshes on one skeleton: %s" % (package, mesh, len(found), [path.rsplit("/", 1)[1] for path in found]))
    if not editor_assets.rename_asset(found[0], wanted):
        log("could not rename %s to %s" % (found[0], wanted))
        return False
    return True


# How each material of the equipment should look: (pattern in its name, RoughnessDark, RoughnessBright, Metallic,
# Emissive). Sleeves and gloves are cloth; guns are worn steel and polymer, kept only part metal because the
# original textures have their lighting painted in and sparkle under a close light when fully metallic; the SI_
# textures are the rifle's lit rate-of-fire display.
EQUIPMENT_LOOKS = [
    (r"si_rof|_si$", 0.5, 0.3, 0.0, 4.0),
    (r"1def|arms|att_01|shadw|heat", 0.8, 0.5, 0.0, 0.0),
    (r"viseur", 0.25, 0.1, 0.3, 0.0),
    (r"", 0.62, 0.4, 0.35, 0.0),
]


def equipment_look(name):
    lowered = name.lower()
    for pattern, dark, bright, metallic, emissive in EQUIPMENT_LOOKS:
        if re.search(pattern, lowered):
            return dark, bright, metallic, emissive
    return 0.8, 0.5, 0.0, 0.0


def set_materials(package, mesh_name, surface, look):
    """The importer's own materials are not compiled for skinned meshes in a game run, so each slot of every
    mesh of the file gets an instance of the project's surface material with the slot's original texture."""
    base = "%s/%s/%s" % (DEST, package, mesh_name)
    if not surface:
        return
    textures = {}
    for path in editor_assets.list_assets(base + "/Textures", recursive=False):
        textures[key(path.rsplit("/", 1)[1].split(".")[0])] = path.split(".")[0]

    for asset_path in skeletal_meshes_of(package, mesh_name):
        mesh = unreal.load_asset(asset_path)
        slots = mesh.get_editor_property("materials")
        names = []
        for slot in slots:
            imported = slot.get_editor_property("material_interface")
            name = str(imported.get_name()) if imported else ""
            names.append(name[3:] if name.startswith("MI_") else name)

        # The original files name a texture after its material; OpenSCCT's own models have a PBR set per
        # material, and their arms reuse an original material's name over a texture set of their own.
        def colour_of(name):
            return textures.get(key(name)) or textures.get(key(name + "_baseColor"))

        claimed = {colour_of(name) for name in names}
        spare = [path for texture, path in sorted(textures.items()) if texture.endswith("basecolor") and path not in claimed]

        for index in range(len(slots)):
            slot = slots[index]  # a copy: it is written back below
            name = names[index]
            if not name:
                continue
            path = "%s/Materials/MI_%s" % (base, name)
            instance = unreal.load_asset(path) if editor_assets.does_asset_exist(path) else asset_tools.create_asset(
                "MI_" + name, base + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            materials_lib.set_material_instance_parent(instance, surface)
            colour = colour_of(name) or (spare[0] if spare else None)
            if colour:
                materials_lib.set_material_instance_texture_parameter_value(instance, "BaseColor", unreal.load_asset(colour))
                normal = colour[:-len("baseColor")] + "normal" if colour.lower().endswith("basecolor") else None
                if normal and editor_assets.does_asset_exist(normal):
                    materials_lib.set_material_instance_texture_parameter_value(instance, "Normal", unreal.load_asset(normal))
            else:
                log("no texture for material %s of %s (have %s)" % (name, mesh_name, sorted(textures)))
            dark, bright, metallic, emissive = look(name)
            materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessDark", dark)
            materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessBright", bright)
            materials_lib.set_material_instance_scalar_parameter_value(instance, "Metallic", metallic)
            materials_lib.set_material_instance_scalar_parameter_value(instance, "Emissive", emissive)
            materials_lib.update_material_instance(instance)
            editor_assets.save_loaded_asset(instance, only_if_is_dirty=False)
            slot.set_editor_property("material_interface", instance)
            slots[index] = slot
        mesh.set_editor_property("materials", slots)
        # Setting the slots does not mark the mesh as changed, so it is saved explicitly.
        editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        bones = len(mesh.skeleton.get_reference_pose().get_bone_names()) if mesh.skeleton else 0
        log("materials set on %s.%s (%s): %d slots, %d bones" % (package, mesh_name, asset_path.rsplit("/", 1)[1], len(slots), bones))


def body_look(_name):
    # Suits and gear: matte cloth with a slight sheen on the brighter, worn parts.
    return 0.8, 0.5, 0.0, 0.0


def import_skins(package, pattern, parent, texture_parameter, scalars):
    """Materials the simulation swaps in by texture name: for every texture of the package's textures folder
    whose name matches, the texture and an instance of parent named MI_<texture> in
    /Game/Imported/Chars/<package>/Skins (the game finds it by the simulation's skin name)."""
    folder = os.path.join(CHARS, package, "textures")
    if not os.path.isdir(folder) or not parent:
        return
    skins = "%s/%s/Skins" % (DEST, package)
    names = [name[:-4] for name in sorted(os.listdir(folder)) if name.lower().endswith(".png") and re.search(pattern, name)]
    tasks = []
    for name in names:
        if editor_assets.does_asset_exist("%s/%s" % (skins, name)):
            continue
        task = unreal.AssetImportTask()
        task.filename = os.path.join(folder, name + ".png")
        task.destination_path = skins
        task.automated = True
        task.save = True
        tasks.append(task)
    if tasks:
        asset_tools.import_asset_tasks(tasks)
    for name in names:
        texture = unreal.load_asset("%s/%s" % (skins, name))
        if not texture:
            log("skin texture %s did not import" % name)
            continue
        path = "%s/MI_%s" % (skins, name)
        instance = unreal.load_asset(path) if editor_assets.does_asset_exist(path) else asset_tools.create_asset(
            "MI_" + name, skins, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        materials_lib.set_material_instance_parent(instance, parent)
        materials_lib.set_material_instance_texture_parameter_value(instance, texture_parameter, texture)
        for parameter, value in scalars.items():
            materials_lib.set_material_instance_scalar_parameter_value(instance, parameter, value)
        materials_lib.update_material_instance(instance)
        editor_assets.save_loaded_asset(instance, only_if_is_dirty=False)
    log("skins of %s: %s" % (package, names))
    return names


def use_lens_materials():
    """The goggle meshes' own material is a lens too: each takes the lens material of its texture."""
    for package, mesh_name in equipment_models():
        if package != "SGoggle":
            continue
        mesh = unreal.load_asset(mesh_path(package, mesh_name))
        if not mesh:
            continue
        slots = mesh.get_editor_property("materials")
        for index in range(len(slots)):
            slot = slots[index]
            current = slot.get_editor_property("material_interface")
            name = str(current.get_name()) if current else ""
            lens = "%s/SGoggle/Skins/%s" % (DEST, name if name.startswith("MI_") else "MI_" + name)
            if editor_assets.does_asset_exist(lens):
                slot.set_editor_property("material_interface", unreal.load_asset(lens))
                slots[index] = slot
        mesh.set_editor_property("materials", slots)
        editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)


surface = unreal.load_asset(SURFACE_MATERIAL)
if surface and not (surface.get_editor_property("used_with_skeletal_mesh") and surface.get_editor_property("used_with_nanite")):
    surface.set_editor_property("used_with_skeletal_mesh", True)
    surface.set_editor_property("used_with_nanite", True)
    materials_lib.recompile_material(surface)
    editor_assets.save_loaded_asset(surface, only_if_is_dirty=False)

import_models(BODIES, None)
for package, mesh_name in BODIES:
    set_materials(package, mesh_name, surface, body_look)

equipment = equipment_models()
import_models(equipment, pipelines_without_animations())
for package, mesh_name in equipment:
    if name_mesh_after_file(package, mesh_name):
        set_materials(package, mesh_name, surface, equipment_look)

# The lit goggle lenses (one colour per vision mode) and the rifle's rate-of-fire display (one per setting).
glow = unreal.load_asset(GLOW_MATERIAL)
if glow:
    import_skins("SGoggle", r".", glow, "Lens", {})
    use_lens_materials()
else:
    log("no %s: run build_equipment_assets.py first (Import-Characters.ps1 does both), then this again, for the goggle lenses" % GLOW_MATERIAL)
import_skins("SWeaponFirst", r"_SI_rof_", surface, "BaseColor", {"Emissive": 4.0, "RoughnessDark": 0.5, "RoughnessBright": 0.3})

registry = unreal.AssetRegistryHelpers.get_asset_registry()
by_class = {}
for asset in registry.get_assets_by_path(DEST, recursive=True):
    by_class.setdefault(str(asset.asset_class_path.asset_name), []).append(str(asset.package_name))
for cls, names in sorted(by_class.items()):
    log("%s: %d e.g. %s" % (cls, len(names), names[:3]))
for path in by_class.get("SkeletalMesh", []):
    mesh = unreal.load_asset(path)
    skeleton = mesh.skeleton
    log("mesh %s bounds %s skeleton %s" % (path, mesh.get_bounds().box_extent, skeleton.get_path_name() if skeleton else None))
editor_assets.save_directory(DEST, only_if_is_dirty=True, recursive=True)
