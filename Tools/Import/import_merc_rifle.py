"""Imports the merc's rifle (our own art, built by Art/Weapons/MercRifle) into the project. Runs inside the
editor's Python, without a window:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_merc_rifle.py <Art/Weapons/MercRifle folder>"

It makes, under /Game/OpenSCCT/Weapons (committed: this is not original game content):

    M_ScctWeapon, M_ScctWeaponGlass     master materials for any weapon painted as base colour + normal + ORM + emissive
    MercRifle/Textures/T_MercRifle_*    the four painted textures
    MercRifle/MI_MercRifle, MI_MercRifleGlass
    MercRifle/SK_MercRifle              skeletal mesh (LOD 0 first person, LOD 1 third person) with its sockets
    MercRifle/L_RifleLook               a small lit level with three copies of the rifle, to look at it in the engine
"""
import json
import os
import sys

import unreal

SOURCE = sys.argv[1]
WEAPONS = "/Game/OpenSCCT/Weapons"
DEST = WEAPONS + "/MercRifle"
MESH = DEST + "/SK_MercRifle"

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


def save(asset):
    # Changes made through set_editor_property do not always mark the asset as changed.
    editor_assets.save_loaded_asset(asset, only_if_is_dirty=False)


# ---------------------------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------------------------

def import_textures():
    tasks = []
    for suffix in ("BaseColor", "Normal", "ORM", "Emissive"):
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SOURCE, "textures", "T_MercRifle_%s.png" % suffix)
        task.destination_path = DEST + "/Textures"
        task.automated = True
        task.save = True
        task.replace_existing = True
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)

    textures = {suffix: unreal.load_asset("%s/Textures/T_MercRifle_%s" % (DEST, suffix)) for suffix in ("BaseColor", "Normal", "ORM", "Emissive")}
    normal, masks = textures["Normal"], textures["ORM"]
    normal.set_editor_property("srgb", False)
    normal.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    # The painter writes +Y up (as Blender reads it); Unreal wants +Y down.
    normal.set_editor_property("flip_green_channel", True)
    masks.set_editor_property("srgb", False)
    masks.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    for texture in textures.values():
        # A weapon that fills a third of the screen keeps its full resolution longer than a wall would.
        try_set(texture, "lod_group", unreal.TextureGroup.TEXTUREGROUP_WEAPON)
        save(texture)
    return textures


# ---------------------------------------------------------------------------------------------------------------
# Master materials
# ---------------------------------------------------------------------------------------------------------------

def build_weapon_material(path, textures, glass):
    """Base colour, normal, ORM (occlusion, roughness, metallic) and emissive textures with a few dials. The glass
    version is see-through and lit, for sight lenses and magazine windows: its emissive texture draws the reticle."""
    if editor_assets.does_asset_exist(path):
        editor_assets.delete_asset(path)
    folder, name = path.rsplit("/", 1)
    material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y):
        return materials_lib.create_material_expression(material, cls, x, y)

    def texture(parameter, asset, sampler, y):
        sample = node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, y)
        sample.set_editor_property("parameter_name", parameter)
        sample.set_editor_property("texture", asset)
        sample.set_editor_property("sampler_type", sampler)
        return sample

    def scalar(parameter, default, y):
        value = node(unreal.MaterialExpressionScalarParameter, -1000, y)
        value.set_editor_property("parameter_name", parameter)
        value.set_editor_property("default_value", default)
        return value

    def colour(parameter, default, y):
        value = node(unreal.MaterialExpressionVectorParameter, -1000, y)
        value.set_editor_property("parameter_name", parameter)
        value.set_editor_property("default_value", unreal.LinearColor(*default))
        return value

    def multiply(a, a_pin, b, b_pin, x, y):
        result = node(unreal.MaterialExpressionMultiply, x, y)
        materials_lib.connect_material_expressions(a, a_pin, result, "A")
        materials_lib.connect_material_expressions(b, b_pin, result, "B")
        return result

    base = texture("BaseColor", textures["BaseColor"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -400)
    tinted = multiply(base, "RGB", colour("Tint", (1, 1, 1, 1), -200), "", -600, -350)
    materials_lib.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    normal = texture("Normal", textures["Normal"], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 0)
    materials_lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    masks = texture("ORM", textures["ORM"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 300)
    materials_lib.connect_material_property(masks, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    roughness = multiply(masks, "G", scalar("RoughnessScale", 1.0, 500), "", -600, 400)
    materials_lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    materials_lib.connect_material_property(masks, "B", unreal.MaterialProperty.MP_METALLIC)

    emissive = texture("Emissive", textures["Emissive"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 700)
    glow = multiply(emissive, "RGB", scalar("EmissiveStrength", 30.0 if glass else 10.0, 900), "", -600, 750)
    glow = multiply(glow, "", colour("EmissiveTint", (1, 1, 1, 1), 1000), "", -400, 800)
    materials_lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if glass:
        # Nearly clear, and opaque where the emissive texture is lit so the reticle does not wash out.
        lit = node(unreal.MaterialExpressionAdd, -600, 1150)
        materials_lib.connect_material_expressions(emissive, "R", lit, "A")
        materials_lib.connect_material_expressions(scalar("Opacity", 0.16, 1200), "", lit, "B")
        opacity = node(unreal.MaterialExpressionSaturate, -400, 1150)
        materials_lib.connect_material_expressions(lit, "", opacity, "")
        materials_lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        # Lit like a surface, with its own reflections: flat translucency would look like tinted film.
        try_set(material, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
        try_set(material, "two_sided", True)

    material.set_editor_property("used_with_skeletal_mesh", True)
    materials_lib.recompile_material(material)
    editor_assets.save_asset(path)
    log("made " + path)
    return material


def material_instance(name, parent, textures):
    path = DEST + "/" + name
    if editor_assets.does_asset_exist(path):
        instance = unreal.load_asset(path)
    else:
        instance = asset_tools.create_asset(name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    materials_lib.set_material_instance_parent(instance, parent)
    for parameter, texture in textures.items():
        materials_lib.set_material_instance_texture_parameter_value(instance, parameter, texture)
    materials_lib.update_material_instance(instance)
    save(instance)
    return instance


# ---------------------------------------------------------------------------------------------------------------
# Skeletal mesh
# ---------------------------------------------------------------------------------------------------------------

def import_mesh(instances, rig):
    for path in editor_assets.list_assets(DEST, recursive=False):
        # A fresh import every time: a re-import would keep old sockets and material slots.
        if path.rsplit("/", 1)[1].startswith(("SK_MercRifle", "MercRifle", "Armature", "T_MercRifle")):
            editor_assets.delete_asset(path.split(".")[0])

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    try_set(pipeline, "use_source_name_for_asset", True)
    common = pipeline.get_editor_property("common_meshes_properties")
    try_set(common, "force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_SKELETAL_MESH)
    try_set(common, "recompute_normals", False)     # the model's normals are authored (hardened bevels)
    try_set(common, "import_lods", False)
    mesh_pipeline = pipeline.get_editor_property("mesh_pipeline")
    try_set(mesh_pipeline, "import_skeletal_meshes", True)
    try_set(mesh_pipeline, "import_static_meshes", False)
    try_set(mesh_pipeline, "create_physics_asset", False)
    material_pipeline = pipeline.get_editor_property("material_pipeline")
    try_set(material_pipeline, "import_materials", False)
    try_set(material_pipeline.get_editor_property("texture_pipeline"), "import_textures", False)
    try_set(pipeline.get_editor_property("animation_pipeline"), "import_animations", False)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE, "MercRifle.fbx")
    task.destination_path = DEST
    task.destination_name = "SK_MercRifle"
    task.automated = True
    task.save = True
    task.replace_existing = True
    task.options = stack
    asset_tools.import_asset_tasks([task])

    found = {}
    for path in editor_assets.list_assets(DEST, recursive=False):
        asset = unreal.load_asset(path)
        log("imported %s (%s)" % (path, type(asset).__name__))
        found.setdefault(type(asset).__name__, []).append(asset)
    mesh = found["SkeletalMesh"][0]
    if mesh.get_path_name().split(".")[0] != MESH:
        editor_assets.rename_asset(mesh.get_path_name().split(".")[0], MESH)
        mesh = unreal.load_asset(MESH)

    # Material slots: the FBX names them after the Blender materials.
    slots = mesh.get_editor_property("materials")
    for index in range(len(slots)):
        slot = slots[index]   # a copy: it is written back below
        name = str(slot.get_editor_property("material_slot_name"))
        slot.set_editor_property("material_interface", instances["glass" if "Glass" in name else "body"])
        slots[index] = slot
        log("material slot %d: %s" % (index, name))
    mesh.set_editor_property("materials", slots)

    # The third-person model as the next level of detail.
    result = unreal.SkeletalMeshEditorSubsystem.import_lod(mesh, 1, os.path.join(SOURCE, "MercRifle_LOD1.fbx").replace("\\", "/"))
    log("LOD 1 import returned %d; the mesh has %d LODs" % (result, unreal.SkeletalMeshEditorSubsystem.get_lod_count(mesh)))

    for item in rig["sockets"]:
        # A socket's name and bone cannot be written from Python: the mesh names a new socket itself, and the
        # editor's own rename and re-parent calls set them.
        socket = unreal.SkeletalMeshSocket(mesh)
        mesh.add_socket(socket, False)
        unreal.SkeletalMeshEditorSubsystem.rename_socket(mesh, socket.socket_name, item["name"])
        socket.set_socket_parent(mesh, item["bone"])
        socket.set_editor_property("relative_location", unreal.Vector(*item["unreal_local_cm"]))
    save(mesh)
    if mesh.skeleton:
        save(mesh.skeleton)

    # What the game code needs to know, as the engine sees it.
    bounds = mesh.get_bounds()
    log("mesh bounds: centre %s extent %s" % (bounds.origin, bounds.box_extent))
    try:
        pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.skeleton)
        for bone in unreal.AnimPoseExtensions.get_bone_names(pose):
            local = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL)
            world = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
            log("bone %s: local %s rot %s scale %s | mesh space %s" % (bone, local.translation, local.rotation.rotator(), local.scale3d, world.translation))
    except Exception as error:  # pylint: disable=broad-except
        log("could not read the reference pose: %s" % error)
    for index in range(mesh.num_sockets()):
        socket = mesh.get_socket_by_index(index)
        log("socket %s on %s at %s" % (socket.socket_name, socket.bone_name, socket.relative_location))
    return mesh


# ---------------------------------------------------------------------------------------------------------------
# Look level
# ---------------------------------------------------------------------------------------------------------------

def build_look_level(mesh):
    """Three copies of the rifle over a dark floor under studio panel lights and two coloured accents. Rendered
    off-screen by Import-MercRifle.ps1 -Look."""
    level_path = DEST + "/L_RifleLook"
    if editor_assets.does_asset_exist(level_path):
        editor_assets.delete_asset(level_path)
    world = asset_tools.create_asset("L_RifleLook", DEST, unreal.World, unreal.WorldFactory())

    def spawn(cls, location, rotation=(0, 0, 0), scale=(1, 1, 1)):
        transform = unreal.Transform(unreal.Vector(*location), unreal.Rotator(roll=rotation[0], pitch=rotation[1], yaw=rotation[2]), unreal.Vector(*scale))
        return unreal.ScctImportLibrary.spawn_actor_in_world(world, cls, transform)

    # A plain dark, slightly glossy floor to stand them over.
    floor_path = DEST + "/M_RifleLookFloor"
    if editor_assets.does_asset_exist(floor_path):
        floor_material = unreal.load_asset(floor_path)
    else:
        floor_material = asset_tools.create_asset("M_RifleLookFloor", DEST, unreal.Material, unreal.MaterialFactoryNew())
        for value, target in ((0.03, unreal.MaterialProperty.MP_BASE_COLOR), (0.3, unreal.MaterialProperty.MP_ROUGHNESS)):
            constant = materials_lib.create_material_expression(floor_material, unreal.MaterialExpressionConstant, -300, 0)
            constant.set_editor_property("r", value)
            materials_lib.connect_material_property(constant, "", target)
        materials_lib.recompile_material(floor_material)
        editor_assets.save_asset(floor_path)
    for location, rotation, scale in (((0, 0, 0), (0, 0, 0), (12, 12, 1)), ((300, 0, 300), (0, 90, 0), (8, 12, 1))):
        floor = spawn(unreal.StaticMeshActor, location, rotation, scale)
        floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
        floor.static_mesh_component.set_material(0, floor_material)

    # The cameras stand on the -X side looking along +X. Left side on (barrel to the left), three-quarter from the
    # muzzle, and pointing away as the merc sees it.
    for label, location, yaw in (("Side", (0, -90, 110), -90), ("ThreeQuarter", (0, 0, 110), 150), ("FirstPerson", (0, 100, 110), 0)):
        actor = spawn(unreal.SkeletalMeshActor, location, (0, 0, yaw))
        actor.set_actor_label("Rifle" + label)
        component = actor.skeletal_mesh_component
        component.set_skeletal_mesh_asset(mesh)
        try_set(component, "forced_lod_model", 1)   # always the first-person model here

    def light(cls, location, rotation, candelas, colour, radius, size=None):
        actor = spawn(cls, location, rotation)
        component = actor.light_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        component.set_intensity(candelas)
        component.set_light_color(unreal.LinearColor(*colour, 1.0))
        component.set_attenuation_radius(radius)
        component.set_cast_shadows(True)
        if size:
            component.set_source_width(size[0])
            component.set_source_height(size[1])
        else:
            component.set_source_radius(10.0)

    # Panel lights, as in a photo studio: metal needs something broad to reflect. They are kept as dim as a lit
    # room in one of the maps, so the glowing parts (reticle, display, lenses) look as they will in a match.
    light(unreal.RectLight, (-170, 0, 280), (0, -45, 0), 800, (1.0, 0.95, 0.88), 1200, (320, 120))     # key, warm white
    light(unreal.RectLight, (0, 0, 340), (0, -90, 0), 450, (1.0, 1.0, 1.0), 1000, (60, 420))           # strip overhead
    light(unreal.RectLight, (170, 170, 210), (0, -22, -135), 450, (0.55, 0.75, 1.0), 1000, (200, 80))  # rim, cool
    light(unreal.PointLight, (-60, 200, 70), (0, 0, 0), 60, (1.0, 0.45, 0.15), 600)                    # accent, amber
    light(unreal.PointLight, (-60, -220, 60), (0, 0, 0), 60, (0.1, 0.8, 0.75), 600)                    # accent, teal

    sky = spawn(unreal.SkyLight, (0, 0, 400))
    sky_component = sky.light_component
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    try_set(sky_component, "real_time_capture", False)
    try_set(sky_component, "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    try_set(sky_component, "cubemap", unreal.load_asset("/Engine/MapTemplates/Sky/DaylightAmbientCubemap"))
    sky_component.set_intensity(0.05)

    post = spawn(unreal.PostProcessVolume, (0, 0, 0))
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in [
        # A fixed exposure (in EV100: the project extends the luminance range), so every shot is comparable.
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        ("auto_exposure_min_brightness", 5.0),
        ("auto_exposure_max_brightness", 5.0),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.8),
        ("vignette_intensity", 0.4),
    ]:
        try_set(settings, "override_" + name, True)
        try_set(settings, name, value)
    post.set_editor_property("settings", settings)

    # No match here: a plain game mode whose pawn is the view pawn, so the screenshot tour can place the camera.
    mode_path = DEST + "/BP_RifleLookGameMode"
    if editor_assets.does_asset_exist(mode_path):
        blueprint = unreal.load_asset(mode_path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.GameModeBase)
        blueprint = asset_tools.create_asset("BP_RifleLookGameMode", DEST, unreal.Blueprint, factory)
        defaults = unreal.get_default_object(blueprint.generated_class())
        defaults.set_editor_property("default_pawn_class", unreal.load_class(None, "/Script/OpenSCCT.ScctViewPawn"))
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        save(blueprint)
    world.get_world_settings().set_editor_property("default_game_mode", blueprint.generated_class())

    editor_assets.save_loaded_asset(world)
    log("saved " + level_path)


def main():
    rig = json.load(open(os.path.join(SOURCE, "rifle_rig.json"), encoding="utf-8"))
    textures = import_textures()
    weapon = build_weapon_material(WEAPONS + "/M_ScctWeapon", textures, glass=False)
    glass = build_weapon_material(WEAPONS + "/M_ScctWeaponGlass", textures, glass=True)
    instances = {"body": material_instance("MI_MercRifle", weapon, textures), "glass": material_instance("MI_MercRifleGlass", glass, textures)}
    mesh = import_mesh(instances, rig)
    build_look_level(mesh)
    editor_assets.save_directory(WEAPONS, only_if_is_dirty=True, recursive=True)
    log("merc rifle imported")


main()
