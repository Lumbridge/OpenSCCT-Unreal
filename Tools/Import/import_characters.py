"""Imports the converted character models (spy and merc) and everything they hold and wear as skeletal meshes.
Runs inside the editor's Python, with no window:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_characters.py <content root>" -nullrhi

What is imported:
  - the bodies SPerso.ATT_01 (spy) and SPerso.DEF_01 (merc), with their animations;
  - the equipment the bridge's catalog lists (Bridge/OpenScct.UnrealBridge/Equipment/EquipmentCatalog.cs): every
    .glb of shared/chars/SWeaponFirst, SGoggle and OpenScctSidearms, the world and first-person models from
    OpenScctRifles/models.json, and the spy's eye-view arms SPerso.ATT_01_fp. The bridge samples their original
    animations directly from the converted files.
  - the goggle lens textures of shared/chars/SGoggle/textures, each with a lens material (the simulation names
    the lens colour per vision mode by texture).

Every file <Package>/<Mesh>.glb becomes /Game/Imported/Chars/<Package>/<Mesh>/SkeletalMeshes/<Mesh> (the name the
game looks for, whatever the mesh is called inside the file), with the original skeleton: the simulation's
poses are computed for it. Nothing here is committed.
"""
import json
import os
import re
import struct
import sys

import unreal

CONTENT_ROOT = sys.argv[1]
CHARS = os.path.join(CONTENT_ROOT, "shared", "chars")
DEST = "/Game/Imported/Chars"
BODIES = [("SPerso", "ATT_01"), ("SPerso", "DEF_01")]

# The same rule as the bridge's catalog: these packages hold equipment, plus the spy's eye-view arms.
EQUIPMENT_PACKAGES = ["SWeaponFirst", "SGoggle", "OpenScctSidearms"]
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


def set_weapon_finishes(package, mesh_name):
    """Restore the converted model's masked gloss or glTF metal/roughness on merc first-person equipment."""
    if not ((package == "SWeaponFirst" and mesh_name.startswith("1DEF_")) or
            (package == "OpenScctSidearms" and mesh_name.endswith("_first"))):
        return
    source = os.path.join(CHARS, package)
    with open(os.path.join(source, mesh_name + ".glb"), "rb") as file:
        file.seek(12)
        length, _ = struct.unpack("<I4s", file.read(8))
        model = json.loads(file.read(length))
    base = "%s/%s/%s" % (DEST, package, mesh_name)
    textures = {key(path.rsplit("/", 1)[1].split(".")[0]): path
                for path in editor_assets.list_assets(base + "/Textures", recursive=False)}
    materials = {}
    for asset_path in skeletal_meshes_of(package, mesh_name):
        for slot in unreal.load_asset(asset_path).get_editor_property("materials"):
            instance = slot.material_interface
            if isinstance(instance, unreal.MaterialInstanceConstant):
                materials[key(str(slot.material_slot_name))] = instance
    for entry in model["materials"]:
        pbr = entry.get("pbrMetallicRoughness", {})
        packed_map = pbr.get("metallicRoughnessTexture")
        if packed_map is not None:
            image = model["images"][model["textures"][packed_map["index"]]["source"]]
            uri = image.get("uri")
        else:
            uri = entry.get("extras", {}).get("scct_spec_mask")
        if not uri:
            continue
        name = os.path.splitext(os.path.basename(uri))[0]
        # Interchange imports glTF PBR maps but does not follow the original reflection masks in extras.
        texture_path = textures.get(key(name))
        if not texture_path:
            task = unreal.AssetImportTask()
            task.filename = os.path.join(source, uri)
            task.destination_path = base + "/Textures"
            task.automated = True
            task.save = True
            asset_tools.import_asset_tasks([task])
            if not task.imported_object_paths:
                raise RuntimeError("could not import weapon finish " + task.filename)
            texture_path = task.imported_object_paths[0]
        texture = unreal.load_asset(texture_path)
        if (texture.get_editor_property("srgb") or
                texture.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS):
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            editor_assets.save_loaded_asset(texture)
        instance = materials[key(entry["name"])]
        materials_lib.set_material_instance_texture_parameter_value(instance, "WeaponFinishTexture", texture)
        roughness_scale = pbr.get("roughnessFactor", 1.0) if packed_map is not None else 1.0
        materials_lib.set_material_instance_scalar_parameter_value(instance, "WeaponPackedPBR", float(packed_map is not None))
        materials_lib.set_material_instance_scalar_parameter_value(instance, "WeaponRoughnessFactor", roughness_scale)
        materials_lib.set_material_instance_scalar_parameter_value(instance, "WeaponMetallicFactor", pbr.get("metallicFactor", 1.0))
        materials_lib.set_material_instance_static_switch_parameter_value(instance, "UseWeaponFinish", True)
        materials_lib.update_material_instance(instance)
        if (not materials_lib.get_material_instance_static_switch_parameter_value(instance, "UseWeaponFinish") or
                materials_lib.get_material_instance_texture_parameter_value(instance, "WeaponFinishTexture") != texture):
            raise RuntimeError("weapon finish unavailable on " + instance.get_path_name())
        editor_assets.save_loaded_asset(instance, only_if_is_dirty=False)
        log("weapon finish %s.%s / %s <- %s" % (package, mesh_name, entry["name"], texture.get_name()))


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
            name_key = key(name)
            if name_key in ("openscctsidearmscasebrass", "openscctsidearmscaseprimer"):
                return textures.get(name_key) or textures.get(name_key[len("openscctsidearms"):])
            return textures.get(name_key) or textures.get(name_key + "basecolor")

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
            materials_lib.set_material_instance_scalar_parameter_value(instance, "Specular", BODY_SPECULAR if look is body_look else 0.5)
            # Masked, with the texture's alpha left out of the mask: the surface material can then thin the mesh
            # out of the picture close to the camera (custom primitive data slot scct_materials.NEAR_FADE_SLOT),
            # which the game asks for on the local player's body and what it carries.
            materials_lib.set_material_instance_scalar_parameter_value(instance, "AlphaMask", 0.0)
            overrides = instance.get_editor_property("base_property_overrides")
            overrides.set_editor_property("override_blend_mode", True)
            overrides.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            instance.set_editor_property("base_property_overrides", overrides)
            materials_lib.update_material_instance(instance)
            editor_assets.save_loaded_asset(instance, only_if_is_dirty=False)
            slot.set_editor_property("material_interface", instance)
            slots[index] = slot
        mesh.set_editor_property("materials", slots)
        # Setting the slots does not mark the mesh as changed, so it is saved explicitly.
        editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        bones = len(mesh.skeleton.get_reference_pose().get_bone_names()) if mesh.skeleton else 0
        log("materials set on %s.%s (%s): %d slots, %d bones" % (package, mesh_name, asset_path.rsplit("/", 1)[1], len(slots), bones))


# How much a body mirrors its surroundings (the engine's Specular; 0.5 is its default for everything else). The
# suits are painted nearly black, so almost all the light a body sends back is this mirror term: at 0.5 a spy under
# an open sky, or beside a lit wall, was a pale grey figure, because the eye is open wide for the dark he stands in.
BODY_SPECULAR = 0.18


def body_look(_name):
    # Suits and gear: matte cloth, a little less matte on the brighter, worn parts.
    return 0.92, 0.62, 0.0, 0.0


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


def import_rifles():
    import long_gun_materials
    import scct_rifle_cosmetics
    scct_rifle_cosmetics.build()
    # The manifest lists the playable models. Auxiliary art (e.g. micro_red_dot.glb) is already
    # skinned into the rifles that use it. Clips are sampled from the original GLB by the bridge.
    package = "OpenScctRifles"
    source = os.path.join(CHARS, package)
    with open(os.path.join(source, "models.json"), encoding="utf-8") as manifest:
        models = [(package, name + suffix) for name in json.load(manifest) for suffix in ("", "_first")]
    for _, name in models:
        if not os.path.isfile(os.path.join(source, name + ".glb")):
            raise RuntimeError("missing rifle model %s/%s.glb" % (package, name))
    import_models(models, pipelines_without_animations())

    # Keep the authored material inputs, including the M4's transparent sight and unlit reticle.
    # Interchange's lit glTF parent lacks the skeletal shader permutation in a cooked game.
    # Enable it on a project-local copy and retain each instance's original texture/parameter bindings.
    gltf = "/InterchangeAssets/gltf/Substrate/M_GLTF"
    target = "%s/%s/Materials/M_RifleGltf" % (DEST, package)
    if editor_assets.does_asset_exist(target):
        parent = unreal.load_asset(target)
    else:
        folder, filename = target.rsplit("/", 1)
        parent = asset_tools.duplicate_asset(filename, folder, unreal.load_object(None, gltf + ".M_GLTF"))
    if not parent:
        raise RuntimeError("could not create the rifle material at " + target)
    if not parent.get_editor_property("used_with_skeletal_mesh"):
        parent.set_editor_property("used_with_skeletal_mesh", True)
        materials_lib.recompile_material(parent)
    editor_assets.save_loaded_asset(parent)

    for _, name in models:
        if not name_mesh_after_file(package, name):
            raise RuntimeError("missing imported mesh " + name)
        meshes = skeletal_meshes_of(package, name)
        for path in meshes:
            mesh = unreal.load_asset(path)
            slots = mesh.get_editor_property("materials")
            restored = False
            for index, slot in enumerate(slots):
                # The original instance is named after the source material. Restore its binding on
                # assets that an earlier import converted to generic equipment materials.
                material_path = "%s/%s/%s/Materials/%s" % (DEST, package, name, slot.material_slot_name)
                material = unreal.load_asset(material_path)
                if not isinstance(material, unreal.MaterialInstanceConstant):
                    raise RuntimeError("missing authored rifle material " + material_path)
                if material.parent.get_path_name() == gltf + ".M_GLTF":
                    materials_lib.set_material_instance_parent(material, parent)
                    materials_lib.update_material_instance(material)
                    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
                if slot.material_interface != material:
                    slot.material_interface = material
                    slots[index] = slot
                    restored = True
            if restored:
                mesh.set_editor_property("materials", slots)
                editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        long_gun_materials.apply(CHARS, package, name, meshes)
        scct_rifle_cosmetics.apply(CHARS, name)
        log("rifle %s: %d skinned parts" % (name, len(meshes)))

if "--long-gun-materials-only" in sys.argv:
    import long_gun_materials
    for base in long_gun_materials.MODELS:
        for name in (base, base + "_first"):
            long_gun_materials.apply(CHARS, "OpenScctRifles", name, skeletal_meshes_of("OpenScctRifles", name))
    sys.exit(0)

if "--rifles-only" in sys.argv:
    import_rifles()
    sys.exit(0)

if "--rifle-attachment-finishes-only" in sys.argv:
    import scct_rifle_cosmetics
    for weapon in scct_rifle_cosmetics.RIFLES:
        for model in (weapon, weapon + "_first"):
            scct_rifle_cosmetics.apply(CHARS, model, attachments_only=True)
    sys.exit(0)

if "--weapon-finishes-only" in sys.argv:
    for package, mesh_name in equipment_models():
        set_weapon_finishes(package, mesh_name)
    sys.exit(0)

surface = unreal.load_asset(SURFACE_MATERIAL)
# The surface material is a committed asset built by scct_materials.py, which gives it the skeletal mesh and
# Nanite usage the characters need. It is never changed or saved from here.
if not surface:
    raise RuntimeError("no %s: run Tools/Import/Import-Assets.ps1 -MaterialsOnly first" % SURFACE_MATERIAL)
if not (surface.get_editor_property("used_with_skeletal_mesh") and surface.get_editor_property("used_with_nanite")):
    log("WARNING: %s lacks the skeletal mesh or Nanite usage: rebuild it with Tools/Import/Import-Assets.ps1 -MaterialsOnly" % SURFACE_MATERIAL)

if "--magnum-reload-only" in sys.argv:
    package, mesh_name = "OpenScctSidearms", "magnum44_first"
    import_models([(package, mesh_name)], pipelines_without_animations())
    if not name_mesh_after_file(package, mesh_name):
        raise RuntimeError("missing imported mesh " + mesh_name)
    set_materials(package, mesh_name, surface, equipment_look)
    set_weapon_finishes(package, mesh_name)
    for case in range(6):
        path = "%s/%s/%s/SkeletalMeshes/SpentCase%d_mesh" % (DEST, package, mesh_name, case)
        mesh = unreal.load_asset(path)
        if not mesh:
            raise RuntimeError("missing improved case mesh at " + path)
        materials = {str(slot.material_interface.get_name()): slot.material_interface
                     for slot in mesh.get_editor_property("materials") if slot.material_interface}
        for name in ("CaseBrass", "CasePrimer"):
            instance = materials.get("MI_OpenScctSidearms_" + name)
            texture = materials_lib.get_material_instance_texture_parameter_value(instance, "BaseColor") if instance else None
            if not texture or str(texture.get_name()) != name:
                raise RuntimeError("missing %s texture binding on %s" % (name, path))
    log("magnum44_first: verified all six brass cases with recessed primer materials")
    sys.exit(0)

import_models(BODIES, None)
for package, mesh_name in BODIES:
    set_materials(package, mesh_name, surface, body_look)

equipment = equipment_models()
import_models(equipment, pipelines_without_animations())
for package, mesh_name in equipment:
    if name_mesh_after_file(package, mesh_name):
        set_materials(package, mesh_name, surface, equipment_look)
        set_weapon_finishes(package, mesh_name)
if os.path.isdir(os.path.join(CHARS, "OpenScctRifles")):
    import_rifles()

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
