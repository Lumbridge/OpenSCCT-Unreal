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
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scct_materials  # noqa: E402  pylint: disable=wrong-import-position

# Light conversion. The original lights were tuned for baked lightmaps with hand-placed fill lights; these
# factors bring them to physical units under Lumen. Tuned by eye on AquaE.
RADIUS_UNITS = 25.0          # world radius = 25 * (LightRadius + 1), as the original engine
CANDELAS_PER_BRIGHTNESS = 0.45
EMISSIVE_UNLIT = 2.0      # screens, lamps and signs that were unlit in the original
EMISSIVE_ADDITIVE = 0.4   # flare and glow sprites: bloom does the rest
EMISSIVE_SELF_ILLUM = 4.0  # the self-lit part of a lit surface whose class names no strength of its own

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
# Materials
# ---------------------------------------------------------------------------------------------------------------

def load_surface_classes():
    """surface_classes.json: the class of every original material and the look of each class."""
    document = json.load(open(os.path.join(HERE, "surface_classes.json")))
    classes = document["classes"]
    chosen = dict(document["materials"])
    chosen.update(document.get("overrides", {}))
    fallback = document.get("fallback", "generic")
    sets = json.load(open(os.path.join(HERE, "pbr_sets.json")))["sets"]

    def look_of(source):
        name = chosen.get(source, fallback)
        return name, classes.get(name) or classes[fallback]

    return look_of, sets


def pan_speed(animation):
    """The original's texture scrolling as UV units per second, or (0, 0). Only straight panners are carried over;
    its oscillators and rotators are rare (a dozen materials) and stay still."""
    if not animation or animation.get("type") != "panner":
        return 0.0, 0.0
    # PanDirection is a rotator; its yaw turns the scroll direction in the texture plane [estimate: the sign of
    # the scroll is not decoded, which only matters for text that scrolls].
    yaw = animation["direction"][1] / 65536.0 * 2.0 * math.pi
    return animation["rate"] * math.cos(yaw), animation["rate"] * math.sin(yaw)


def build_materials(material_doc):
    """One material instance per glTF material. Returns {imported material asset name: instance}."""
    additive = {key(entry["key"]) for entry in material_doc.get("additive", []) if "key" in entry}
    additive |= {key(entry["name"]) for entry in material_doc.get("additive", []) if "name" in entry}
    look_of, sets = load_surface_classes()
    parents = {name: unreal.load_asset(path) for name, path in
               (("surface", scct_materials.SURFACE), ("glass", scct_materials.GLASS), ("water", scct_materials.WATER))}
    if not all(parents.values()):
        raise RuntimeError("master materials are missing: run Tools/Import/Import-Assets.ps1 first")

    def by_key(folder_path):
        found = {}
        for asset_path in editor_assets.list_assets(folder_path):
            found[key(asset_path.rsplit("/", 1)[1].split(".")[0])] = asset_path
        return found

    # Textures a material needs beyond its colour map (self-illumination and its mask) are not in the glTF.
    extra_files = set()
    for record in material_doc["materials"].values():
        for flag in ("self_illum", "self_illum_mask"):
            path = record["flags"].get(flag)
            if isinstance(path, str) and os.path.exists(os.path.join(RENDER, path)):
                extra_files.add(os.path.join(RENDER, path))
        sheet = (record["flags"].get("flipbook") or {}).get("texture")
        if sheet and os.path.exists(os.path.join(RENDER, sheet)):
            extra_files.add(os.path.join(RENDER, sheet))
    textures = by_key(DEST + "/world/Textures")
    missing_extra = sorted(path for path in extra_files if key(os.path.splitext(os.path.basename(path))[0]) not in textures)
    if missing_extra:
        import_files(missing_extra, DEST + "/Extra")
    extra = by_key(DEST + "/Extra")
    normal_maps = by_key(DEST + "/Normals")
    detail_folder = scct_materials.THIRD_PARTY + "/ambientCG/T_%s_%s"

    def texture_of(path):
        stem = key(os.path.splitext(os.path.basename(path))[0])
        asset = textures.get(stem) or extra.get(stem)
        return unreal.load_asset(asset) if asset else None

    instances = {}
    counts = {}
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
        lib_clear = getattr(materials_lib, "clear_all_material_instance_parameters", None)
        if lib_clear:
            lib_clear(instance)   # a re-import starts from the parent's defaults, not last time's values

        class_name, look = look_of(record.get("source") or material_name)
        is_additive = key(imported) in additive
        unlit = bool(flags.get("unlit")) or is_additive
        translucent = bool(flags.get("translucent"))
        # Glass and water get their own materials only where the original drew a see-through, lit surface.
        kind = look.get("kind", "surface")
        if kind == "glass" and not (translucent and not is_additive):
            kind = "plain"
        if kind == "water" and (unlit or flags.get("masked")):
            kind = "plain"
        counts[class_name] = counts.get(class_name, 0) + 1

        def scalar(parameter, value):
            materials_lib.set_material_instance_scalar_parameter_value(instance, parameter, float(value))

        def texture(parameter, asset):
            if asset:
                materials_lib.set_material_instance_texture_parameter_value(instance, parameter, asset)

        texture_file = flags.get("texture")
        base = texture_of(texture_file) if texture_file else None
        blend = None
        if kind == "glass":
            materials_lib.set_material_instance_parent(instance, parents["glass"])
            texture("BaseColor", base)
            scalar("Roughness", look["rough_dark"])
            scalar("OpacityFloor", look.get("opacity_floor", 0.12))
            scalar("Emissive", EMISSIVE_ADDITIVE if unlit else 0.0)
        elif kind == "water":
            materials_lib.set_material_instance_parent(instance, parents["water"])
            texture("BaseColor", base)
            scalar("Roughness", look["rough_dark"])
            # Water the original drew solid hides what is under it; keep it nearly so.
            scalar("Opacity", 0.55 if translucent else 0.93)
        else:
            materials_lib.set_material_instance_parent(instance, parents["surface"])
            texture("BaseColor", base)
            if texture_file:
                stem = os.path.splitext(os.path.basename(texture_file))[0]
                normal = normal_maps.get(key(stem + "_N"))
                texture("Normal", unreal.load_asset(normal) if normal else None)
            scalar("RoughnessDark", look["rough_dark"])
            scalar("RoughnessBright", look["rough_bright"])
            scalar("Metallic", look["metallic"])
            scalar("Specular", look.get("specular", 0.5))
            scalar("MacroNormal", look.get("macro_normal", 1.0))

            # The detail layer: only on lit surfaces (a glow or a decal has no grain of its own).
            detail = look.get("detail") if kind == "surface" and not unlit else None
            if detail:
                texture("DetailNormal", unreal.load_asset(detail_folder % (detail, "N")))
                texture("DetailORD", unreal.load_asset(detail_folder % (detail, "ORD")))
                scalar("DetailTiling", 1.0 / look.get("tile_cm", sets[detail]["tile_cm"]))
                scalar("DetailNormalStrength", look.get("detail_normal", 1.0))
                scalar("DetailRoughness", look.get("detail_rough", 0.5))
                scalar("DetailColour", look.get("detail_colour", 0.5))
                scalar("DetailOcclusion", look.get("detail_ao", 0.5))

            # Light given off: whole-surface for what the original drew unlit, masked for its self-illumination.
            glow = look.get("emissive")
            scalar("Emissive", EMISSIVE_ADDITIVE if is_additive else (glow or EMISSIVE_UNLIT) if unlit else 0.0)
            self_lit = flags.get("self_illum")
            if isinstance(self_lit, str) and texture_of(self_lit) and not unlit:
                texture("SelfIllumTexture", texture_of(self_lit))
                mask = flags.get("self_illum_mask")
                texture("SelfIllumMask", texture_of(mask) if isinstance(mask, str) else None)
                scalar("SelfIllum", glow or EMISSIVE_SELF_ILLUM)

            pan_u, pan_v = pan_speed(flags.get("uv_anim"))
            scalar("PanU", pan_u)
            scalar("PanV", pan_v)
            flipbook = flags.get("flipbook")
            sheet = texture_of(flipbook["texture"]) if flipbook and flipbook.get("texture") else None
            if sheet:
                texture("BaseColor", sheet)
                scalar("FlipColumns", flipbook["cols"])
                scalar("FlipRows", flipbook["rows"])
                scalar("FlipRate", flipbook["fps"])
                scalar("FlipFrames", flipbook["frames"])

            if is_additive:
                blend = unreal.BlendMode.BLEND_ADDITIVE
            elif translucent:
                blend = unreal.BlendMode.BLEND_TRANSLUCENT
            elif flags.get("masked"):
                blend = unreal.BlendMode.BLEND_MASKED

        overrides = instance.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_blend_mode", blend is not None)
        if blend is not None:
            overrides.set_editor_property("blend_mode", blend)
        two_sided = bool(flags.get("two_sided")) and kind != "glass"   # the glass master is two-sided already
        overrides.set_editor_property("override_two_sided", two_sided)
        overrides.set_editor_property("two_sided", two_sided)
        instance.set_editor_property("base_property_overrides", overrides)

        materials_lib.update_material_instance(instance)
        instances[key(imported)] = instance
    log("surface classes: " + ", ".join("%s %d" % item for item in sorted(counts.items(), key=lambda item: -item[1])))
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

    instances = build_materials(material_doc)
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
            # The surface material's detail layer is world-aligned; on something that moves it must travel along.
            component.set_default_custom_primitive_data_float(scct_materials.LOCAL_DETAIL_SLOT, 1.0)
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
        if instance.get("projectorOf"):
            # A copy of a surface that carries a projected texture (rain streaks, caustics). Placed as geometry it
            # would be an opaque duplicate fighting the real surface; projectors come back as decals.
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
    # Every light of the map is placed, also those the original used only for characters (apply_static false):
    # the lights its lightmaps were baked from are mostly not in the map file, and the character lights sit at the
    # real fixtures, so together they are the best record of how each room was lit.
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
        # The map's colours come from hue and saturation, in display (gamma) space.
        colour = [channel ** 2.2 for channel in light["color"]]
        component.set_light_color(unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        component.set_attenuation_radius(radius * 1.5)
        component.set_source_radius(6.0)
        component.set_soft_source_radius(12.0)
        component.set_cast_shadows(True)
        try_set(component, "volumetric_scattering_intensity", 1.5 if spot else 0.6)
        if spot:
            # The original's cone: full angle 1.15 * acos((1 - LightCone/256)^2) (OpenSCCT docs/RENDERING.md).
            # Its inner cone is a twentieth of that; a softer edge reads better with real shadows.
            full = math.degrees(1.15 * math.acos((1.0 - min(light["cone"], 255) / 256.0) ** 2))
            outer = max(5.0, min(80.0, full * 0.5))
            component.set_outer_cone_angle(outer)
            component.set_inner_cone_angle(outer * 0.45)
        # LightType 0 is "none": a light that exists to be switched on by the simulation.
        if not light.get("initially_on", True) or light.get("ltype") == 0:
            component.set_visibility(False)
        count += 1
    log("lights: %d" % count)

    # ---- atmosphere ---------------------------------------------------------------------------------------------
    # The original's sky box is not imported (it is a 2005 backdrop). Each map gets a real sky instead: an HDRI
    # panorama on a dome, a sky light that throws that panorama's light, and a moon where the sky has one.
    # Interiors need no special case: Lumen lets sky light in only where the level is open to it.
    skies = json.load(open(os.path.join(HERE, "map_skies.json")))
    sky_kind = skies["maps"].get(MAP, skies["default"])
    sky_look = skies["kinds"][sky_kind]
    panorama_path = scct_materials.THIRD_PARTY + "/PolyHaven/" + sky_look["panorama"]
    tint = unreal.LinearColor(sky_look["tint"][0], sky_look["tint"][1], sky_look["tint"][2], 1.0)

    sky_material_path = DEST + "/Materials/MI_Sky"
    if editor_assets.does_asset_exist(sky_material_path):
        sky_material = unreal.load_asset(sky_material_path)
    else:
        sky_material = asset_tools.create_asset("MI_Sky", DEST + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    materials_lib.set_material_instance_parent(sky_material, unreal.load_asset(scct_materials.SKY))
    materials_lib.set_material_instance_texture_parameter_value(sky_material, "Panorama", unreal.load_asset(panorama_path))
    materials_lib.set_material_instance_vector_parameter_value(sky_material, "Tint", tint)
    for name, value in (("Brightness", sky_look["brightness"]), ("StarBrightness", sky_look["stars"]), ("Rotation", sky_look["rotation"])):
        materials_lib.set_material_instance_scalar_parameter_value(sky_material, name, float(value))
    materials_lib.update_material_instance(sky_material)

    # The dome: far outside every map (the largest is under 300 m across), and nothing but a picture: no shadow,
    # no collision, and kept out of the lighting scene so it cannot block the sky light it stands for.
    dome = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    dome.set_actor_label("SkyDome")
    dome.set_folder_path("Atmosphere")
    dome.set_actor_scale3d(unreal.Vector(20000, 20000, 20000))
    dome_component = dome.static_mesh_component
    dome_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Sphere"))
    dome_component.set_material(0, sky_material)
    dome_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    dome_component.set_cast_shadow(False)
    for name in ("visible_in_ray_tracing", "affect_dynamic_indirect_lighting", "affect_distance_field_lighting"):
        try_set(dome_component, name, False)

    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, 0, 0))
    sky.set_folder_path("Atmosphere")
    sky_component = sky.light_component
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    try_set(sky_component, "real_time_capture", False)
    try_set(sky_component, "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    try_set(sky_component, "cubemap", unreal.load_asset(panorama_path + "_light"))
    try_set(sky_component, "source_cubemap_angle", float(sky_look["rotation"]))
    sky_component.set_intensity(sky_look["sky_light"])
    sky_component.set_light_color(tint)

    moon_look = sky_look.get("moon")
    if moon_look:
        moon = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 4000), unreal.Rotator(roll=0, pitch=moon_look["pitch"], yaw=moon_look["yaw"]))
        moon.set_actor_label("Moon")
        moon.set_folder_path("Atmosphere")
        moon_component = moon.light_component
        moon_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        moon_component.set_intensity(moon_look["lux"])
        moon_component.set_light_color(unreal.LinearColor(moon_look["colour"][0], moon_look["colour"][1], moon_look["colour"][2], 1.0))
        moon_component.set_cast_shadows(True)
        # A wide source: moonlight through haze throws soft-edged shadows.
        try_set(moon_component, "light_source_angle", 2.0)
        try_set(moon_component, "volumetric_scattering_intensity", 1.0)
    log("sky: %s%s" % (sky_kind, " with a moon" if moon_look else ""))

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
        # In EV100 (the project extends the luminance range). The eye adapts well into the dark, so shadows
        # stay readable when you stand in them, but not far up, so bright rooms do not grey out.
        ("auto_exposure_min_brightness", -2.5),
        ("auto_exposure_max_brightness", 2.0),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.9),
        ("bloom_threshold", -1.0),
        ("vignette_intensity", 0.45),
        ("film_grain_intensity", 0.12),
        ("scene_fringe_intensity", 0.25),
        ("lens_flare_intensity", 0.4),
        ("ambient_occlusion_intensity", 0.6),
        ("motion_blur_amount", 0.35),
        # Glass and water show the room in them, not only the sky.
        ("lumen_front_layer_translucency_reflections", True),
    ]:
        try_set(settings, "override_" + name, True)
        try_set(settings, name, value)
    post.set_editor_property("settings", settings)

    world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/OpenSCCT.ScctGameMode"))

    editor_assets.save_loaded_asset(world)
    editor_assets.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("saved " + level_path)


main()
