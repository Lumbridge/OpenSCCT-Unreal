"""Builds an Unreal level from a converted OpenSCCT map. Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_map.py <content root> <Map> <normals folder>"

It imports the map's geometry (already in simulation coordinates, which are Unreal's), gives every surface a lit
material, places the static meshes and movers, turns the map's lights into dynamic Unreal lights and adds the
atmosphere. The result goes to /Game/Imported/Maps/<Map>/ (never committed: it is original game content).
"""
import json
import math
import os
import re
import struct
import sys

import unreal

CONTENT_ROOT, MAP, NORMALS_DIR = sys.argv[1], sys.argv[2], sys.argv[3]
RENDER = os.path.join(CONTENT_ROOT, "svmap", MAP, "render")
DEST = "/Game/Imported/Maps/" + MAP
SURFACE_MATERIAL = "/Game/OpenSCCT/Materials/M_ScctSurface"

# Light conversion. The original lights were tuned for baked lightmaps with hand-placed fill lights; these
# factors bring them to physical units under Lumen. Tuned by eye on AquaE.
RADIUS_UNITS = 25.0          # world radius = 25 * (LightRadius + 1), as the original engine
CANDELAS_PER_BRIGHTNESS = 0.45
EMISSIVE_UNLIT = 3.0      # screens, lamps and signs that were unlit in the original
EMISSIVE_ADDITIVE = 1.0   # flare and glow sprites: bloom does the rest

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
materials_lib = unreal.MaterialEditingLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def try_set(target, name, value):
    """Sets a property, reporting (not failing on) a name this engine version does not have."""
    try:
        target.set_editor_property(name, value)
    except Exception as error:  # pylint: disable=broad-except
        log("property not set: %s (%s)" % (name, str(error).split(":")[0]))


def sanitize(name):
    """The asset name the engine's importer gives a glTF node, material or image name."""
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def key(name):
    """A name reduced to letters and digits. The engine's importer keeps some punctuation (a hyphen) and
    replaces other (a dot, a hash), so imported assets are matched to the map's names through this."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


def read_glb_json(path):
    with open(path, "rb") as f:
        f.read(12)
        length, _ = struct.unpack("<I4s", f.read(8))
        return json.loads(f.read(length))


def import_files(files, destination):
    tasks = []
    for path in files:
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = destination
        task.automated = True
        task.save = True
        task.replace_existing = True
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)


def rotator(units):
    """[pitch, yaw, roll] in the original's 65536-per-turn units to an Unreal rotator."""
    scale = 360.0 / 65536.0
    return unreal.Rotator(roll=units[2] * scale, pitch=units[0] * scale, yaw=units[1] * scale)


# ---------------------------------------------------------------------------------------------------------------
# Master material
# ---------------------------------------------------------------------------------------------------------------

def ensure_surface_material():
    if editor_assets.does_asset_exist(SURFACE_MATERIAL):
        return unreal.load_asset(SURFACE_MATERIAL)

    folder, name = SURFACE_MATERIAL.rsplit("/", 1)
    material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y):
        return materials_lib.create_material_expression(material, cls, x, y)

    base = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, -200)
    base.set_editor_property("parameter_name", "BaseColor")

    tint = node(unreal.MaterialExpressionVectorParameter, -900, 50)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))

    colour = node(unreal.MaterialExpressionMultiply, -550, -150)
    materials_lib.connect_material_expressions(base, "RGB", colour, "A")
    materials_lib.connect_material_expressions(tint, "", colour, "B")
    materials_lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

    opacity = node(unreal.MaterialExpressionScalarParameter, -900, 250)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)
    alpha = node(unreal.MaterialExpressionMultiply, -550, 200)
    materials_lib.connect_material_expressions(base, "A", alpha, "A")
    materials_lib.connect_material_expressions(opacity, "", alpha, "B")
    materials_lib.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    materials_lib.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    normal = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 450)
    normal.set_editor_property("parameter_name", "Normal")
    normal.set_editor_property("texture", unreal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    materials_lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    # Darker parts of a surface read as rougher grime, brighter parts as worn, smoother material.
    rough_dark = node(unreal.MaterialExpressionScalarParameter, -900, 700)
    rough_dark.set_editor_property("parameter_name", "RoughnessDark")
    rough_dark.set_editor_property("default_value", 0.85)
    rough_bright = node(unreal.MaterialExpressionScalarParameter, -900, 800)
    rough_bright.set_editor_property("parameter_name", "RoughnessBright")
    rough_bright.set_editor_property("default_value", 0.45)
    roughness = node(unreal.MaterialExpressionLinearInterpolate, -550, 720)
    materials_lib.connect_material_expressions(rough_dark, "", roughness, "A")
    materials_lib.connect_material_expressions(rough_bright, "", roughness, "B")
    materials_lib.connect_material_expressions(base, "G", roughness, "Alpha")
    materials_lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metallic = node(unreal.MaterialExpressionScalarParameter, -900, 950)
    metallic.set_editor_property("parameter_name", "Metallic")
    metallic.set_editor_property("default_value", 0.0)
    materials_lib.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

    emissive_strength = node(unreal.MaterialExpressionScalarParameter, -900, 1100)
    emissive_strength.set_editor_property("parameter_name", "Emissive")
    emissive_strength.set_editor_property("default_value", 0.0)
    emissive = node(unreal.MaterialExpressionMultiply, -550, 1050)
    materials_lib.connect_material_expressions(colour, "", emissive, "A")
    materials_lib.connect_material_expressions(emissive_strength, "", emissive, "B")
    materials_lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # One material serves level surfaces and characters.
    material.set_editor_property("used_with_skeletal_mesh", True)
    materials_lib.recompile_material(material)
    editor_assets.save_asset(SURFACE_MATERIAL)
    log("made " + SURFACE_MATERIAL)
    return material


# Surface looks by the words in a texture's name: (pattern, RoughnessDark, RoughnessBright, Metallic).
SURFACE_LOOKS = [
    (r"metal|steel|iron|alu|chrome|grille|grid|pipe|tuyau|vent|inox|rail", 0.55, 0.22, 0.9),
    (r"glass|verre|vitre|window|mirror|miroir", 0.12, 0.04, 0.0),
    (r"water|eau|aqua_water|liquid", 0.08, 0.03, 0.0),
    (r"marble|marbre|tile|carrel|ceram|floor|sol|polish", 0.45, 0.12, 0.0),
    (r"wood|bois|parquet|plank", 0.65, 0.35, 0.0),
    (r"plastic|plastiq|screen|ecran|ordi|comput", 0.45, 0.2, 0.0),
    (r"beton|concrete|wall|mur|brick|brique|rock|stone|pierre|gravier|plaster", 0.92, 0.7, 0.0),
    (r"fabric|tissu|carpet|moquette|cloth|rideau|curtain", 0.98, 0.9, 0.0),
]


def surface_look(name):
    lowered = name.lower()
    for pattern, dark, bright, metallic in SURFACE_LOOKS:
        if re.search(pattern, lowered):
            return dark, bright, metallic
    return 0.85, 0.5, 0.0


def build_materials(parent, material_doc):
    """One material instance per glTF material. Returns {imported material asset name: instance}."""
    additive = {key(entry["key"]) for entry in material_doc.get("additive", []) if "key" in entry}
    additive |= {key(entry["name"]) for entry in material_doc.get("additive", []) if "name" in entry}

    def by_key(folder_path):
        found = {}
        for asset_path in editor_assets.list_assets(folder_path):
            found[key(asset_path.rsplit("/", 1)[1].split(".")[0])] = asset_path
        return found

    textures = by_key(DEST + "/world/Textures")
    normal_maps = by_key(DEST + "/Normals")
    instances = {}
    folder = DEST + "/Materials"
    for material_name, record in material_doc["materials"].items():
        flags = record["flags"]
        imported = sanitize(material_name)
        name = "MI_" + imported
        path = folder + "/" + name
        if editor_assets.does_asset_exist(path):
            instance = unreal.load_asset(path)
        else:
            instance = asset_tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        materials_lib.set_material_instance_parent(instance, parent)

        texture_file = flags.get("texture")
        if texture_file:
            stem = os.path.splitext(os.path.basename(texture_file))[0]
            texture = unreal.load_asset(textures[key(stem)]) if key(stem) in textures else None
            if texture:
                materials_lib.set_material_instance_texture_parameter_value(instance, "BaseColor", texture)
            normal = unreal.load_asset(normal_maps[key(stem + "_N")]) if key(stem + "_N") in normal_maps else None
            if normal:
                materials_lib.set_material_instance_texture_parameter_value(instance, "Normal", normal)

        dark, bright, metallic = surface_look(material_name)
        materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessDark", dark)
        materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessBright", bright)
        materials_lib.set_material_instance_scalar_parameter_value(instance, "Metallic", metallic)

        is_additive = key(imported) in additive
        unlit = bool(flags.get("unlit")) or is_additive
        emissive = EMISSIVE_ADDITIVE if is_additive else EMISSIVE_UNLIT if unlit else 0.0
        materials_lib.set_material_instance_scalar_parameter_value(instance, "Emissive", emissive)

        overrides = instance.get_editor_property("base_property_overrides")
        blend = None
        if is_additive:
            blend = unreal.BlendMode.BLEND_ADDITIVE
        elif flags.get("translucent"):
            blend = unreal.BlendMode.BLEND_TRANSLUCENT
        elif flags.get("masked"):
            blend = unreal.BlendMode.BLEND_MASKED
        overrides.set_editor_property("override_blend_mode", blend is not None)
        if blend is not None:
            overrides.set_editor_property("blend_mode", blend)
        overrides.set_editor_property("override_two_sided", bool(flags.get("two_sided")))
        overrides.set_editor_property("two_sided", bool(flags.get("two_sided")))
        instance.set_editor_property("base_property_overrides", overrides)

        materials_lib.update_material_instance(instance)
        instances[key(imported)] = instance
    return instances


# ---------------------------------------------------------------------------------------------------------------
# Import
# ---------------------------------------------------------------------------------------------------------------

def main():
    glb_path = os.path.join(RENDER, "world.glb")
    gltf = read_glb_json(glb_path)
    material_doc = json.load(open(os.path.join(RENDER, "materials.json")))
    lighting = json.load(open(os.path.join(RENDER, "lighting.json")))

    log("importing geometry of " + MAP)
    import_files([glb_path], DEST)

    normals = [os.path.join(NORMALS_DIR, f) for f in sorted(os.listdir(NORMALS_DIR)) if f.endswith("_N.png")]
    import_files(normals, DEST + "/Normals")
    for path in editor_assets.list_assets(DEST + "/Normals"):
        texture = unreal.load_asset(path)
        if isinstance(texture, unreal.Texture2D) and texture.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)

    parent = ensure_surface_material()
    instances = build_materials(parent, material_doc)
    log("materials: %d" % len(instances))

    # Every mesh gets the new materials, and Nanite for crisp shadows and cheap overdraw.
    meshes = {}
    unmatched = set()
    for path in editor_assets.list_assets(DEST + "/world/StaticMeshes"):
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        meshes[key(str(mesh.get_name()))] = mesh
        translucent = False
        for index, slot in enumerate(mesh.static_materials):
            current = slot.material_interface
            replacement = instances.get(key(str(current.get_name()))) if current else None
            if replacement:
                mesh.set_material(index, replacement)
                if replacement.get_blend_mode() in (unreal.BlendMode.BLEND_TRANSLUCENT, unreal.BlendMode.BLEND_ADDITIVE):
                    translucent = True
            elif current:
                unmatched.add(str(current.get_name()))
        if not translucent:
            settings = mesh.get_editor_property("nanite_settings")
            if not settings.enabled:
                settings.enabled = True
                mesh.set_editor_property("nanite_settings", settings)
    if unmatched:
        log("materials without a record (kept as imported): %s" % sorted(unmatched)[:10])

    # ---- the level --------------------------------------------------------------------------------------------
    level_path = DEST + "/" + MAP
    # The editor's level tools are not available without its window, so the world is made as an asset and
    # actors are spawned straight into it.
    if editor_assets.does_asset_exist(level_path):
        editor_assets.delete_asset(level_path)
    world = asset_tools.create_asset(MAP, DEST, unreal.World, unreal.WorldFactory())

    def spawn(cls, location, rotation):
        transform = unreal.Transform(location, rotation, unreal.Vector(1, 1, 1))
        return unreal.ScctImportLibrary.spawn_actor_in_world(world, cls, transform)

    def place_mesh(mesh, label, location, rotation, scale, folder, movable=False):
        actor = spawn(unreal.StaticMeshActor, location, rotation)
        actor.static_mesh_component.set_static_mesh(mesh)
        actor.set_actor_scale3d(scale)
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        actor.tags = [unreal.Name("Scct:" + label)]
        component = actor.static_mesh_component
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)  # the simulation owns collision
        if movable:
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
        return actor

    for node in gltf["nodes"]:
        role = node.get("extras", {}).get("svmap", {})
        if role.get("role") == "bsp" and not role.get("sky"):
            mesh = meshes.get(key(node["name"]))
            if mesh:
                place_mesh(mesh, node["name"], unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1), "Geometry/World")

    placed = missing = 0
    for instance in gltf["extras"]["svmap"]["instances"]:
        if instance.get("hidden") or instance.get("skyZone"):
            continue
        mesh = meshes.get(key(instance["mesh"]))
        if not mesh:
            missing += 1
            continue
        r = instance["rotation"]
        rotation = rotator([r["Pitch"], r["Yaw"], r["Roll"]])
        scale = unreal.Vector(*instance.get("scale", [1, 1, 1]))
        location = unreal.Vector(*instance["location"])
        pivot = instance.get("prePivot")
        if pivot:
            # world = ((local - PrePivot) * Scale) rotated + Location
            offset = unreal.Vector(pivot[0] * scale.x, pivot[1] * scale.y, pivot[2] * scale.z)
            location = location - unreal.MathLibrary.greater_greater_vector_rotator(offset, rotation)
        mover = instance.get("kind") == "mover"
        place_mesh(mesh, instance["actor"], location, rotation, scale, "Geometry/Movers" if mover else "Geometry/Meshes", movable=mover or bool(instance.get("gameplay")))
        placed += 1
    log("placed %d meshes, %d without a mesh asset" % (placed, missing))

    # ---- lights -----------------------------------------------------------------------------------------------
    count = 0
    for light in lighting["lights"]:
        if light.get("hidden"):
            continue
        spot = light["light_type"] == "spot"
        cls = unreal.SpotLight if spot else unreal.PointLight
        actor = spawn(cls, unreal.Vector(*light["location"]), rotator(light["rotation"]))
        actor.set_actor_label(light["name"])
        actor.set_folder_path("Lights/Triggered" if light.get("triggered") else "Lights")
        actor.tags = [unreal.Name("Scct:" + light["name"]), unreal.Name("ScctTag:" + str(light.get("tag", "")))]

        component = actor.light_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        radius = RADIUS_UNITS * (light["radius"] + 1.0)
        component.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        candelas = light["brightness"] * CANDELAS_PER_BRIGHTNESS * (radius / 500.0) ** 2
        component.set_intensity(candelas)
        colour = light["color"]
        component.set_light_color(unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        component.set_attenuation_radius(radius * 1.5)
        component.set_source_radius(6.0)
        component.set_soft_source_radius(12.0)
        component.set_cast_shadows(True)
        try_set(component, "volumetric_scattering_intensity", 1.5 if spot else 0.6)
        if spot:
            outer = max(5.0, min(80.0, light["cone"] / 255.0 * 90.0))
            component.set_outer_cone_angle(outer)
            component.set_inner_cone_angle(outer * 0.55)
        if not light.get("initially_on", True):
            component.set_visibility(False)
        count += 1
    log("lights: %d" % count)

    # ---- atmosphere ---------------------------------------------------------------------------------------------
    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, 0, 0))
    sky.set_folder_path("Atmosphere")
    sky_component = sky.light_component
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    try_set(sky_component, "real_time_capture", False)
    sky_component.set_intensity(0.05)
    sky_component.set_light_color(unreal.LinearColor(0.35, 0.45, 0.7, 1.0))

    fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    fog.set_folder_path("Atmosphere")
    fog_component = fog.component
    try_set(fog_component, "fog_density", 0.006)
    try_set(fog_component, "fog_height_falloff", 0.02)
    try_set(fog_component, "fog_inscattering_luminance", unreal.LinearColor(0.02, 0.03, 0.05, 1.0))
    try_set(fog_component, "enable_volumetric_fog", True)
    try_set(fog_component, "volumetric_fog_scattering_distribution", 0.6)
    try_set(fog_component, "volumetric_fog_extinction_scale", 1.2)

    post = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    post.set_folder_path("Atmosphere")
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in [
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        # A narrow range: the eye adapts a little, but dark rooms stay dark. This is a stealth game.
        ("auto_exposure_min_brightness", 0.7),
        ("auto_exposure_max_brightness", 1.6),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.9),
        ("bloom_threshold", -1.0),
        ("vignette_intensity", 0.45),
        ("film_grain_intensity", 0.12),
        ("scene_fringe_intensity", 0.25),
        ("lens_flare_intensity", 0.4),
        ("ambient_occlusion_intensity", 0.6),
        ("motion_blur_amount", 0.35),
    ]:
        try_set(settings, "override_" + name, True)
        try_set(settings, name, value)
    post.set_editor_property("settings", settings)

    world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/OpenSCCT.ScctGameMode"))

    editor_assets.save_loaded_asset(world)
    editor_assets.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("saved " + level_path)


main()
