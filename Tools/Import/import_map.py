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
# What prepare_rain_cover.py wrote for this map (the heights of what stands in the rain's way), if it ran.
RAIN_COVER_DIR = sys.argv[4] if len(sys.argv) > 4 else ""
RENDER = os.path.join(CONTENT_ROOT, "svmap", MAP, "render")
DEST = "/Game/Imported/Maps/" + MAP
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scct_materials  # noqa: E402  pylint: disable=wrong-import-position
import scct_fx  # noqa: E402  pylint: disable=wrong-import-position

# Light conversion. The original's lights are not physical: a light fades as 1 - (d / R)^2 and is gone at its
# radius R, and the artists placed them for that (a lamp sits a hand's width under its ceiling and still lights the
# whole room evenly). A physical inverse-square light at the same spot burns a white patch into the ceiling and
# leaves the far half of the room black, so the lights keep the original's kind of falloff, a little tighter so
# that pools of light and shadow read. Everything after that is real: shadows, bounce light, reflections.
RADIUS_UNITS = 25.0          # world radius = 25 * (LightRadius + 1), as the original engine
LIGHT_FALLOFF = 1.6          # light = (1 - (d / R)^2) ^ this; the original's is 1
# Light arriving right at a LightBrightness 255 lamp. This sets where lit rooms sit against the night sky, the moon
# and the glowing surfaces below (all in the same units), inside the exposure range of the post-process volume.
LIGHT_PER_BRIGHTNESS = 5.0 / 255.0
# The map's light colours are strong (they multiplied 2005 textures in display space). Under bounce lighting a
# strongly coloured lamp turns a whole room one hue, so part of the colour is given up toward white. (The colour
# counts more than once here: the lamp's light, its bounces off walls it has already tinted, and the baked fill,
# which carries the same tint. At 0.7 SancE's break room was one green and CineE's backstage one blue. The game's
# copy of this number is LevelLightSaturation in Play/ScctLevelStateSubsystem.cpp.)
LIGHT_SATURATION = 0.55
# A "special lit" light lit only the surfaces marked special lit (a car's own tail-light glow, a sign's housing),
# none of the room and none of the characters. Nothing of ours carries that mark, so such a light is kept on a
# lighting channel nothing receives: placed (the level state can still switch it) but lighting no one.
SPECIAL_LIT_CHANNEL = 2
# A lamp the original baked its lightmaps from (apply_static) is already in the baked fill (BAKED_FILL below).
# Measured in BankP's guard room, where 103 of 108 lights are such lamps: the fill alone gives a scene luminance of
# 0.010 and the lamps another 0.070, twice what the whole baked light stood for. They keep this share, for the
# shadows, the shine and the characters. (ScctRuntimeMapActor.cpp has the same two numbers.)
STATIC_LIGHT_SHARE = 0.4
# A light the original put on characters only (apply_ingame without apply_static) stands at a real fixture in the
# maps whose baking lamps are gone from the file, and is the best record of the room's light there. In a map that
# still has its baking lamps (more than half of its lights) it is a fill for the characters and lit no wall:
# BankP's roof, dark in its lightmaps, was flooded pale grey by one hung 5 m over it. There it keeps this share.
CHARACTER_FILL_SHARE = 0.25
# A light with a small radius (many maps hang one of radius 25 or 75 at each lamp, to light the lamp's own housing
# and whoever walks under it) puts all its light on the patch of wall beside it. The original could not draw
# that patch brighter than twice its texture; here it is a white hole with a ruled rim once the eye has opened for
# the corridor around it. Below this radius a light is weakened in step with its radius and its edge is softened.
SMALL_LIGHT_RADIUS = 300.0
SMALL_LIGHT_FALLOFF = 3.0
# Glow strengths are brightnesses in the finished picture, not in the scene (scct_materials.Graph.in_the_picture):
# 1 is the texture as painted at any exposure, more than 1 burns toward white and blooms. What the original drew
# unlit is also shaded unlit here, so a screen in a lit room is not its picture plus the room's light on top.
EMISSIVE_UNLIT = 0.9      # screens, lamps and signs that were unlit in the original
EMISSIVE_ADDITIVE = 0.35  # flare and glow sprites, holograms: bloom does the rest
EMISSIVE_SELF_ILLUM = 2.0  # the self-lit part of a lit surface whose class names no strength of its own
# Additive materials that are painted light: beam and flare cards modelled into the level (French "rayon": ray).
# They get the glow-card material (scct_effects.py) instead of the surface material, which draws them as solids.
GLOW_CARDS = re.compile(r"beam|rayon|flare|lenseffect|dca_fx", re.IGNORECASE)
GLOW_CARD_BRIGHTNESS = 0.2
# Additive sheets laid over a floor to draw the play of light under water (AquaE's Caustic1 group). At a flare's
# strength they burn the floor to a white slab; they are a shimmer on it.
CAUSTIC_SHEETS = re.compile(r"caustic", re.IGNORECASE)
EMISSIVE_CAUSTICS = 0.08

# The original's skyline: buildings and hills modelled in the sky zone, a small diorama the engine drew behind
# everything as seen from one fixed point (the SkyZoneInfo). The sky itself is not imported (each map gets a real
# one), but the skyline is brought back as real geometry far outside the level: the diorama is blown up about
# the sky zone's eye point until its widest mesh stands this many times the level's own radius away (within the
# two bounds, in cm), with the eye point moved to the middle of the level. Seen from there it is exactly the
# original's view; elsewhere it shifts a little, as a real skyline would.
BACKDROP_MESHES = re.compile(r"skyline|warp|imm", re.IGNORECASE)          # by the mesh's own name
BACKDROP_NOT = re.compile(r"sky(?!line)|hemis|cloud|moon|lune|star", re.IGNORECASE)   # the sky itself
BACKDROP_SKY_CUT = 2.0   # a HorizonCut (M_ScctSurface) above the whole texture: the part is not drawn
BACKDROP_LEVEL_RADII = 4.0
BACKDROP_DISTANCE = (60000.0, 150000.0)
# A skyline is neither a lamp in the room nor part of the sky dome: its glow goes half the way with the exposure.
BACKDROP_GLOW = 0.85
BACKDROP_GLOW_IN_PICTURE = 0.5

# What a detail set is scaled to before a surface class's amounts apply (see fetch_assets.measure_set): the
# variation a set with real character has. A weaker set is brought up to it, at most this many times (beyond that
# only its JPEG noise would grow); a stronger one is left as it is.
DETAIL_TARGETS = {"normal_rms": 0.15, "rough_std": 0.10, "luma_std": 0.08}
DETAIL_MAX_GAIN = 5.0

# How much of the original's baked light (lightmaps, baked vertex colours) glows from the surfaces that carried it,
# in the units of the lights above: about 1.6 would be all of it. It is a fill under the real lights, which do the
# shadows and the shine, so it stays well below that.
BAKED_FILL = 0.5
# The least light a lit surface gives off, of its own colour, in the units of the lights above (M_ScctSurface, "the
# least light"): what the original's zone ambient did. A room with no surviving lamp is dim shapes once the eye
# has opened as far as it goes (Source/OpenSCCT/Play/ScctExposure.cpp sets that range for every map), not black.
AMBIENT_FLOOR = 0.012
# Water the original drew at full brightness (a pool in a night club): how much of its colour it gives off, in the
# picture. And the same for the haze sheets it hung in deep water.
WATER_GLOW = 0.22
HAZE_GLOW = 0.3
HAZE_SOFT_DEPTH = 220.0

# The level's motion blur (the engine's own default is 0.5): how much of a frame's motion is smeared, and the most a
# pixel is smeared by, in percent of the picture's width.
MOTION_BLUR = 0.12
MOTION_BLUR_MOST = 1.5

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

    for record in sets.values():
        stats = record.get("stats", {})
        record["gain"] = {name: min(DETAIL_MAX_GAIN, max(1.0, target / max(stats.get(name, target), 1e-4)))
                          for name, target in DETAIL_TARGETS.items()}

    def look_of(source):
        name = chosen.get(source, fallback)
        return name, classes.get(name) or classes[fallback]

    return look_of, sets


def pan_speed(animation):
    """The original's texture scrolling as UV units per second, or (0, 0): a TexPanner (PanDirection is a rotator,
    the scroll is its unit vector's X and Y times PanRate) or a TexFriendlyPanner (PanU, PanV over its scale, times
    the rate), as the Godot client's UvAnim.From reads them. Oscillators and rotators are uv_anim_vectors'."""
    kind = (animation or {}).get("type")
    if kind == "panner":
        pitch = animation["direction"][0] / 65536.0 * 2.0 * math.pi
        yaw = animation["direction"][1] / 65536.0 * 2.0 * math.pi
        return animation["rate"] * math.cos(pitch) * math.cos(yaw), animation["rate"] * math.cos(pitch) * math.sin(yaw)
    if kind == "friendly_panner":
        rate = animation.get("rate", 0.0)
        return (rate * animation.get("pan_u", 0.0) / max(1e-6, animation.get("scale_u", 1.0)),
                rate * animation.get("pan_v", 0.0) / max(1e-6, animation.get("scale_v", 1.0)))
    return 0.0, 0.0


def texture_size(texture, cells=(1, 1)):
    """A texture's (width, height) in texels, of one flipbook cell when it is a sheet of cells; (128, 128), the
    size the stock offsets assume, when the texture is not known."""
    if texture is None:
        return 128.0, 128.0
    return (max(1.0, texture.blueprint_get_size_x() / max(1, cells[0])),
            max(1.0, texture.blueprint_get_size_y() / max(1, cells[1])))


def uv_anim_vectors(animation, size, with_panner=False):
    """The material's AnimA, AnimB, AnimC (scct_materials.uv_anim_code) for the original's TexOscillator or
    TexRotator, or None. `size` is the texture's size in texels: the original's stretch and rotation pivots are in
    texels. A panner is only packed here when `with_panner` (a sequence item); the material's own panner is
    PanU, PanV."""
    kind = (animation or {}).get("type")
    zero = [0.0, 0.0, 0.0, 0.0]
    if kind == "oscillator":
        number = lambda name: float(animation.get(name) or 0.0)
        return ([number("UOscillationAmplitude"), number("UOscillationRate"), number("UOscillationType"), number("UOscillationPhase")],
                [number("VOscillationAmplitude"), number("VOscillationRate"), number("VOscillationType"), float(scct_materials.UV_ANIM_OSCILLATOR)],
                [number("VOscillationPhase"), 0.0, number("UOffset") / size[0], number("VOffset") / size[1]])
    if kind == "rotator":
        # TR_ConstantlyRotating (1) is the only rotation type the converted maps carry; a fixed rotation is baked
        # into the UVs, and an oscillating one (2) turns at its rate as the Godot client turns it.
        rate = animation["rotation"][1] / 65536.0 * 2.0 * math.pi
        return ([rate, 0.0, 0.0, 0.0], [0.0, 0.0, 0.0, float(scct_materials.UV_ANIM_ROTATOR)],
                [0.0, 0.0, float(animation.get("u_offset", 0.0)) / size[0], float(animation.get("v_offset", 0.0)) / size[1]])
    if with_panner and kind in ("panner", "friendly_panner"):
        pan_u, pan_v = pan_speed(animation)
        return [pan_u, pan_v, 0.0, 0.0], [0.0, 0.0, 0.0, float(scct_materials.UV_ANIM_PANNER)], zero
    return None


def build_materials(material_doc):
    """One material instance per glTF material. Returns {imported material asset name: instance}."""
    additive = {key(entry["key"]) for entry in material_doc.get("additive", []) if "key" in entry}
    additive |= {key(entry["name"]) for entry in material_doc.get("additive", []) if "name" in entry}
    look_of, sets = load_surface_classes()
    # The level surfaces' lightmap page, by material (the converter makes one material per texture and page).
    lightmap_of = {key(entry["name"]): entry["lightmap"] for entry in material_doc.get("lightmapped", [])}
    parents = {name: unreal.load_asset(path) for name, path in
               (("surface", scct_materials.SURFACE), ("glass", scct_materials.GLASS), ("water", scct_materials.WATER),
                ("card", scct_fx.scct_effects.GLOW_CARD))}
    if not all(parents.values()):
        raise RuntimeError("master materials are missing: run Tools/Import/Import-Assets.ps1 and Import-Effects.ps1 first")

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
        # A MaterialSequence's items each have their own texture (or AnimNext sheet).
        for item in ((record["flags"].get("sequences") or {}).get("diffuse") or {}).get("items", []):
            for path in (item.get("texture"), (item.get("flipbook") or {}).get("texture")):
                if path and os.path.exists(os.path.join(RENDER, path)):
                    extra_files.add(os.path.join(RENDER, path))
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

    def vector(instance, parameter, values):
        materials_lib.set_material_instance_vector_parameter_value(instance, parameter, unreal.LinearColor(*[float(v) for v in values]))

    # How many of the original's moving textures were carried over, and how many stayed still (on a master that
    # has no such animation: glow cards, glass, water).
    animated = {"rotators": 0, "oscillators": 0, "sequences": 0, "left still": 0}

    def animate(instance, flags, base):
        """The material's oscillator or rotator, and its diffuse MaterialSequence. Surface master only."""
        moving = uv_anim_vectors(flags.get("uv_anim"), texture_size(base))
        if moving:
            for parameter, values in zip(("AnimA", "AnimB", "AnimC"), moving):
                vector(instance, parameter, values)
            animated["rotators" if flags["uv_anim"]["type"] == "rotator" else "oscillators"] += 1
        sequence = (flags.get("sequences") or {}).get("diffuse")
        items = (sequence or {}).get("items") or []
        if not items:
            return
        if len(items) > scct_materials.SEQUENCE_SLOTS:
            log("sequence %s has %d items, the first %d are shown" % (sequence.get("source"), len(items), scct_materials.SEQUENCE_SLOTS))
            items = items[:scct_materials.SEQUENCE_SLOTS]
        materials_lib.set_material_instance_static_switch_parameter_value(instance, "Sequence", True)
        times = [max(0.0, float(item.get("time") or 0.0)) for item in items]
        vector(instance, "SeqInfo", [len(items), sum(times), 0.0 if sequence.get("loop") is False else 1.0,
                                     1.0 if sequence.get("paused") else 0.0])
        padded_times = times + [0.0] * (12 - len(times))
        fades = [1.0 if item.get("action") == 1 else 0.0 for item in items] + [0.0] * (12 - len(items))
        for row in range(3):
            vector(instance, "SeqTimes%d" % row, padded_times[row * 4:row * 4 + 4])
            vector(instance, "SeqFades%d" % row, fades[row * 4:row * 4 + 4])
        for slot, item in enumerate(items):
            flipbook = item.get("flipbook")
            sheet = texture_of(flipbook["texture"]) if flipbook and flipbook.get("texture") else None
            picture = sheet or texture_of(item["texture"])
            if picture:
                materials_lib.set_material_instance_texture_parameter_value(instance, "SeqTex%d" % slot, picture)
            else:
                log("sequence %s: no texture for item %d (%s)" % (sequence.get("source"), slot, item["texture"]))
            cells = (flipbook["cols"], flipbook["rows"]) if sheet else (1, 1)
            vector(instance, "Seq%dFlip" % slot, [cells[0], cells[1], flipbook["frames"] if sheet else 1, flipbook["fps"] if sheet else 0.0])
            moving = uv_anim_vectors(item.get("anim"), texture_size(picture, cells), with_panner=True)
            for parameter, values in zip(("AnimA", "AnimB", "AnimC"), moving or ([0] * 4, [0] * 4, [0] * 4)):
                vector(instance, "Seq%d%s" % (slot, parameter), values)
        animated["sequences"] += 1

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
        if kind == "water" and (is_additive or flags.get("masked")):
            kind = "plain"
        if kind == "haze" and not translucent:
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
        if is_additive and not flags.get("flipbook") and GLOW_CARDS.search(material_name):
            materials_lib.set_material_instance_parent(instance, parents["card"])
            texture("BaseColor", base)
            scalar("Brightness", GLOW_CARD_BRIGHTNESS)
            pan_u, pan_v = pan_speed(flags.get("uv_anim"))
            scalar("PanU", pan_u)
            scalar("PanV", pan_v)
            kind = "card"
        elif kind == "glass":
            materials_lib.set_material_instance_parent(instance, parents["glass"])
            texture("BaseColor", base)
            scalar("Roughness", look["rough_dark"])
            scalar("OpacityFloor", look.get("opacity_floor", 0.12))
            scalar("Emissive", EMISSIVE_ADDITIVE if unlit else 0.0)
        elif kind == "water":
            materials_lib.set_material_instance_parent(instance, parents["water"])
            texture("BaseColor", base)
            scalar("Roughness", look["rough_dark"])
            # How much of what is under it shows through deep water, and through a hand's depth of it. Water the
            # original drew solid hides what is under it (often nothing is modelled there); it only thins at its
            # edges.
            scalar("Opacity", look.get("deep_opacity", 0.82) if translucent else 1.0)
            scalar("ShallowOpacity", look.get("shallow_opacity", 0.12) if translucent else 0.8)
            scalar("Glow", WATER_GLOW if unlit else 0.0)
            pan_u, pan_v = pan_speed(flags.get("uv_anim"))
            scalar("PanU", pan_u)
            scalar("PanV", pan_v)
        elif kind == "haze":
            # A sheet of coloured haze: as thin as the original painted it, its own colour and nothing else (no
            # mirror at a glancing angle, no light on it), fading where it meets things.
            materials_lib.set_material_instance_parent(instance, parents["glass"])
            texture("BaseColor", base)
            scalar("Opacity", look.get("opacity", 1.0))
            scalar("OpacityFloor", 0.0)
            scalar("GlanceOpacity", 0.0)
            scalar("SoftDepth", look.get("soft_depth", HAZE_SOFT_DEPTH))
            scalar("Emissive", look.get("emissive", HAZE_GLOW))
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
            tint = look.get("tint")
            if tint:
                materials_lib.set_material_instance_vector_parameter_value(instance, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))

            # The detail layer: only on lit surfaces (a glow or a decal has no grain of its own).
            detail = look.get("detail") if kind == "surface" and not unlit else None
            if detail:
                texture("DetailNormal", unreal.load_asset(detail_folder % (detail, "N")))
                texture("DetailORD", unreal.load_asset(detail_folder % (detail, "ORD")))
                scalar("DetailTiling", 1.0 / look.get("tile_cm", sets[detail]["tile_cm"]))
                gain = sets[detail]["gain"]
                scalar("DetailNormalStrength", look.get("detail_normal", 1.0) * gain["normal_rms"])
                scalar("DetailRoughness", look.get("detail_rough", 0.5) * gain["rough_std"])
                scalar("DetailColour", look.get("detail_colour", 0.5) * gain["luma_std"])
                scalar("DetailOcclusion", look.get("detail_ao", 0.5))

            # Light given off: whole-surface for what the original drew unlit, masked for its self-illumination.
            glow = look.get("emissive")
            caustics = is_additive and CAUSTIC_SHEETS.search(record.get("source") or material_name)
            scalar("Emissive", EMISSIVE_CAUSTICS if caustics else EMISSIVE_ADDITIVE if is_additive
                   else (glow or EMISSIVE_UNLIT) if unlit else 0.0)
            self_lit = flags.get("self_illum")
            if isinstance(self_lit, str) and texture_of(self_lit) and not unlit:
                texture("SelfIllumTexture", texture_of(self_lit))
                mask = flags.get("self_illum_mask")
                texture("SelfIllumMask", texture_of(mask) if isinstance(mask, str) else None)
                scalar("SelfIllum", glow or EMISSIVE_SELF_ILLUM)

            if not unlit:
                scalar("AmbientFloor", AMBIENT_FLOOR)
                scalar("BakedFill", BAKED_FILL)
                page = lightmap_of.get(key(imported))
                page = texture_of(page) if page else None
                if page:
                    # Display values, like the colour textures (the glTF import took the page for a data map).
                    if not page.get_editor_property("srgb"):
                        page.set_editor_property("srgb", True)
                        page.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
                    texture("Lightmap", page)

            pan_u, pan_v = pan_speed(flags.get("uv_anim"))
            scalar("PanU", pan_u)
            scalar("PanV", pan_v)
            animate(instance, flags, base)
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

        if kind in ("card", "glass", "water", "haze") and (
                (flags.get("uv_anim") or {}).get("type") in ("oscillator", "rotator") or (flags.get("sequences") or {}).get("diffuse")):
            animated["left still"] += 1
            log("%s: its %s stays still on the %s master" % (material_name, "sequence" if flags.get("sequences") else flags["uv_anim"]["type"], kind))

        overrides = instance.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_blend_mode", blend is not None)
        if blend is not None:
            overrides.set_editor_property("blend_mode", blend)
        two_sided = bool(flags.get("two_sided")) and kind not in ("glass", "haze", "card")   # those masters are two-sided already
        overrides.set_editor_property("override_two_sided", two_sided)
        overrides.set_editor_property("two_sided", two_sided)
        # Unlit in the original, unlit here: only its own glow. (Glass, water and cards have their own masters.)
        shade_unlit = (unlit and kind in ("surface", "plain")) or kind == "haze"
        overrides.set_editor_property("override_shading_model", shade_unlit)
        overrides.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT if shade_unlit
                                      else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        instance.set_editor_property("base_property_overrides", overrides)

        materials_lib.update_material_instance(instance)
        instances[key(imported)] = instance
    log("surface classes: " + ", ".join("%s %d" % item for item in sorted(counts.items(), key=lambda item: -item[1])))
    log("texture animation: " + ", ".join("%s %d" % item for item in animated.items()))
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

    # Every mesh gets the new materials, and Nanite for crisp shadows and cheap overdraw. Nanite draws only
    # opaque and masked materials (anything else shows the engine's checkerboard and a "missing usage flag
    # Nanite" warning per material), so a mesh with any other material is not a Nanite mesh. The setting is
    # written both ways: a re-import keeps the mesh asset, and with it whatever an older import decided. The
    # game's level state has the same rule for materials that change in play (ScctLevelStateSubsystem.cpp,
    # LevelAvoidNanite).
    nanite_blends = (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED)
    nanite_on = nanite_off = 0
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
                current = replacement
            elif current:
                unmatched.add(str(current.get_name()))
            if current and current.get_blend_mode() not in nanite_blends:
                translucent = True
        settings = mesh.get_editor_property("nanite_settings")
        if settings.enabled == translucent:
            settings.enabled = not translucent
            mesh.set_editor_property("nanite_settings", settings)
        nanite_on += 0 if translucent else 1
        nanite_off += 1 if translucent else 0
    log("meshes: %d Nanite, %d drawn the ordinary way (glass, glow, cards)" % (nanite_on, nanite_off))
    if unmatched:
        log("materials without a record (kept as imported): %s" % sorted(unmatched)[:10])

    # ---- the level --------------------------------------------------------------------------------------------
    level_path = DEST + "/" + MAP
    # The editor's level tools are not available without its window, so the world is made as an asset and
    # actors are spawned straight into it.
    if editor_assets.does_asset_exist(level_path):
        editor_assets.delete_asset(level_path)
    world = asset_tools.create_asset(MAP, DEST, unreal.World, unreal.WorldFactory())

    def spawn(cls, location, rotation, class_turn=False):
        """Places an actor at exactly this location and rotation. The engine lays a spawn transform over the
        class's own default one, and some classes have a turned default (a spot light and a decal point down,
        pitch -90; a directional light is pitched -46 and scaled): left in, every spot light and projected
        picture of a map pointed the wrong way. `class_turn` keeps the class's default in for the one caller
        tuned with it."""
        transform = unreal.Transform(location, rotation, unreal.Vector(1, 1, 1))
        actor = unreal.ScctImportLibrary.spawn_actor_in_world(world, cls, transform)
        if not class_turn:
            actor.set_actor_rotation(rotation, False)
            actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
        return actor

    authored_meshes = set(material_doc.get("authoredColorMeshes", []))
    vertex_lit_meshes = {entry["name"] for entry in material_doc.get("vertexLitMeshes", [])}

    def vertex_light_kind(name, instance):
        if instance.get("authoredColors") or name in authored_meshes:
            return 2.0
        return 1.0 if instance.get("vertexLit") or name in vertex_lit_meshes else 0.0

    def place_mesh(mesh, label, location, rotation, scale, folder, movable=False, vertex_kind=0.0, tags=None):
        actor = spawn(unreal.StaticMeshActor, location, rotation)
        actor.static_mesh_component.set_static_mesh(mesh)
        actor.set_actor_scale3d(scale)
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        actor.tags = [unreal.Name("Scct:" + label)] + (tags or [])
        component = actor.static_mesh_component
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)  # the simulation owns collision
        if movable:
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
            # The surface material's detail layer is world-aligned; on something that moves it must travel along.
            component.set_default_custom_primitive_data_float(scct_materials.LOCAL_DETAIL_SLOT, 1.0)
        if vertex_kind:
            component.set_default_custom_primitive_data_float(scct_materials.BAKED_VERTEX_SLOT, vertex_kind)
        return actor

    for node in gltf["nodes"]:
        role = node.get("extras", {}).get("svmap", {})
        if role.get("role") == "bsp" and not role.get("sky"):
            mesh = meshes.get(key(node["name"]))
            if mesh:
                place_mesh(mesh, node["name"], unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1), "Geometry/World",
                           vertex_kind=vertex_light_kind(node["name"], role))

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
        place_mesh(mesh, instance["actor"], location, rotation, scale, "Geometry/Movers" if mover else "Geometry/Meshes", movable=mover or bool(instance.get("gameplay")),
                   vertex_kind=vertex_light_kind(instance["mesh"], instance), tags=scct_fx.instance_tags(instance))
        placed += 1
    log("placed %d meshes, %d without a mesh asset" % (placed, missing))

    # ---- the skyline ------------------------------------------------------------------------------------------
    sky_eye = (lighting.get("sky") or {}).get("location")
    backdrop_rules = json.load(open(os.path.join(HERE, "map_skies.json"))).get("backdrop", {})
    skip_backdrop = set(key(name) for name in backdrop_rules.get("skip", []))
    horizon_cut = {key(name): float(cut) for name, cut in backdrop_rules.get("horizon_cut", {}).items() if not name.startswith("about")}
    horizon_key = {key(name): float(cut) for name, cut in backdrop_rules.get("horizon_key", {}).items() if not name.startswith("about")}
    backdrop_source = {"MI_" + sanitize(name): record.get("source") or name for name, record in material_doc["materials"].items()}
    cut_sky = []
    backdrop = []
    for instance in gltf["extras"]["svmap"]["instances"]:
        own_name = instance["mesh"].split("#")[0].split(".")[-1]
        if (not instance.get("skyZone") or instance.get("hidden") or instance.get("kind") != "staticmesh"
                or not BACKDROP_MESHES.search(own_name) or BACKDROP_NOT.search(own_name) or key(own_name) in skip_backdrop):
            continue
        mesh = meshes.get(key(instance["mesh"]))
        if mesh:
            backdrop.append((instance, mesh))
    if sky_eye and backdrop:
        box_low = [min(i["location"][axis] for i in gltf["extras"]["svmap"]["instances"] if not i.get("skyZone") and not i.get("hidden")) for axis in range(3)]
        box_high = [max(i["location"][axis] for i in gltf["extras"]["svmap"]["instances"] if not i.get("skyZone") and not i.get("hidden")) for axis in range(3)]
        centre = [(box_low[axis] + box_high[axis]) * 0.5 for axis in range(3)]
        level_radius = 0.5 * math.hypot(box_high[0] - box_low[0], box_high[1] - box_low[1])
        widest = 1.0
        for instance, mesh in backdrop:
            bounds = mesh.get_bounds()
            scale = instance.get("scale", [1, 1, 1])
            reach = bounds.sphere_radius * max(abs(scale[0]), abs(scale[1]), abs(scale[2]))
            widest = max(widest, math.hypot(instance["location"][0] - sky_eye[0], instance["location"][1] - sky_eye[1]) + reach)
        distance = min(max(BACKDROP_LEVEL_RADII * level_radius, BACKDROP_DISTANCE[0]), BACKDROP_DISTANCE[1])
        grow = distance / widest
        glowing = set()
        for instance, mesh in backdrop:
            r = instance["rotation"]
            rotation = rotator([r["Pitch"], r["Yaw"], r["Roll"]])
            own = instance.get("scale", [1, 1, 1])
            scale = unreal.Vector(own[0] * grow, own[1] * grow, own[2] * grow)
            location = unreal.Vector(*[centre[axis] + (instance["location"][axis] - sky_eye[axis]) * grow for axis in range(3)])
            pivot = instance.get("prePivot")
            if pivot:
                offset = unreal.Vector(pivot[0] * scale.x, pivot[1] * scale.y, pivot[2] * scale.z)
                location = location - unreal.MathLibrary.greater_greater_vector_rotator(offset, rotation)
            actor = place_mesh(mesh, instance["actor"], location, rotation, scale, "Atmosphere/Skyline", vertex_kind=vertex_light_kind(instance["mesh"], instance),
                               tags=[unreal.Name("ScctBackdrop")])
            component = actor.static_mesh_component
            # A picture behind the level: it throws no shadow into it and is no part of its bounce light.
            component.set_cast_shadow(False)
            for name in ("visible_in_ray_tracing", "affect_dynamic_indirect_lighting", "affect_distance_field_lighting"):
                try_set(component, name, False)
            for slot in mesh.static_materials:
                material = slot.material_interface
                if isinstance(material, unreal.MaterialInstanceConstant) and material.get_path_name() not in glowing:
                    glowing.add(material.get_path_name())
                    # Parts of a skyline that are the sky itself (a painted star sphere, a cloud band) are not drawn,
                    # and a panorama with a city under its painted sky keeps only the city: the real sky shows. A
                    # panorama of black hills before a painted glow keeps only what is dark below the cut as well.
                    source = backdrop_source.get(material.get_name(), "")
                    cut = BACKDROP_SKY_CUT if BACKDROP_NOT.search(source.split(".")[-1]) else horizon_cut.get(key(source), 0.0)
                    if cut > 0.0:
                        materials_lib.set_material_instance_scalar_parameter_value(material, "HorizonCut", cut)
                        dark = horizon_key.get(key(source), 0.0) if cut < BACKDROP_SKY_CUT else 0.0
                        materials_lib.set_material_instance_scalar_parameter_value(material, "HorizonKey", dark)
                        overrides = material.get_editor_property("base_property_overrides")
                        overrides.set_editor_property("override_blend_mode", True)
                        overrides.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
                        material.set_editor_property("base_property_overrides", overrides)
                        materials_lib.update_material_instance(material)
                        cut_sky.append("%s %.2f%s" % (source.split(".")[-1], cut, " dark below %.4f" % dark if dark > 0.0 else ""))
                    if materials_lib.get_material_instance_scalar_parameter_value(material, "Emissive") > 0.0:
                        materials_lib.set_material_instance_scalar_parameter_value(material, "Emissive", BACKDROP_GLOW)
                        materials_lib.set_material_instance_scalar_parameter_value(material, "GlowInPicture", BACKDROP_GLOW_IN_PICTURE)
                        materials_lib.update_material_instance(material)
        log("skyline: %d meshes of the sky zone stand %.0f m out, %.0f times their size (%s)" % (
            len(backdrop), distance / 100.0, grow, ", ".join(sorted(set(i["mesh"].split("#")[0].split(".")[-1] for i, _ in backdrop)))))
        log("skyline sky left out: " + (", ".join(sorted(set(cut_sky))) or "none"))
    else:
        log("skyline: none")

    # ---- effects ----------------------------------------------------------------------------------------------
    # What the original drew besides surfaces and lights (scct_fx.py). Their textures are mostly not in the
    # geometry file, so each is imported the first time something asks for it.
    effect_textures = {}

    def effect_texture(file):
        stem = key(os.path.splitext(os.path.basename(file))[0])
        if stem not in effect_textures:
            found = None
            for folder in (DEST + "/world/Textures", DEST + "/Extra"):
                for asset_path in editor_assets.list_assets(folder):
                    if key(asset_path.rsplit("/", 1)[1].split(".")[0]) == stem:
                        found = asset_path
            source = os.path.join(RENDER, file)
            if not found and os.path.exists(source):
                import_files([source], DEST + "/Extra")
                found = DEST + "/Extra/" + sanitize(os.path.splitext(os.path.basename(file))[0])
            effect_textures[stem] = unreal.load_asset(found) if found else None
            if not effect_textures[stem]:
                log("effect texture missing: " + file)
        return effect_textures[stem]

    effects = scct_fx.MapEffects(MAP, RENDER, DEST, spawn, effect_texture, import_files, log, rain_cover=RAIN_COVER_DIR)
    # ---- lights -----------------------------------------------------------------------------------------------
    # Every light of the map is placed, also those the original used only for characters (apply_static false):
    # the lights its lightmaps were baked from are mostly not in the map file, and the character lights sit at the
    # real fixtures, so together they are the best record of how each room was lit.
    count = 0
    placed = [light for light in lighting["lights"] if not light.get("hidden")]
    baking = sum(1 for light in placed if light.get("apply_static"))
    fill_share = CHARACTER_FILL_SHARE if baking * 2 > len(placed) else 1.0
    log("lights: %d of %d are lamps the lightmaps were baked from (share %.2f); characters-only lights keep %.2f" % (
        baking, len(placed), STATIC_LIGHT_SHARE, fill_share))
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
        component.set_editor_property("use_inverse_squared_falloff", False)
        small = min(radius / SMALL_LIGHT_RADIUS, 1.0)
        component.set_editor_property("light_falloff_exponent", LIGHT_FALLOFF + (SMALL_LIGHT_FALLOFF - LIGHT_FALLOFF) * (1.0 - small))
        share = STATIC_LIGHT_SHARE if light.get("apply_static") else fill_share if light.get("apply_ingame") else 1.0
        component.set_intensity(light["brightness"] * LIGHT_PER_BRIGHTNESS * small * share)
        # The map's colours come from hue and saturation, in display (gamma) space.
        colour = [channel ** 2.2 for channel in light["color"]]
        grey = 0.2126 * colour[0] + 0.7152 * colour[1] + 0.0722 * colour[2]
        colour = [grey + (channel - grey) * LIGHT_SATURATION for channel in colour]
        component.set_light_color(unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        component.set_attenuation_radius(radius)
        component.set_source_radius(6.0)
        component.set_soft_source_radius(12.0)
        component.set_cast_shadows(not light.get("special_lit"))
        if light.get("special_lit"):
            # Nothing receives it, so it has no shadow to cast either (a shadowed light costs its shadow maps
            # whether or not anything it lights is drawn).
            channels = unreal.LightingChannels()
            channels.set_editor_property("channel0", False)
            channels.set_editor_property("channel%d" % SPECIAL_LIT_CHANNEL, True)
            component.set_editor_property("lighting_channels", channels)
            try_set(component, "volumetric_scattering_intensity", 0.0)
            try_set(component, "affect_global_illumination", False)
        else:
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
        effects.dress_light(light, actor, LIGHT_PER_BRIGHTNESS)
        count += 1
    log("lights: %d, %d with a beam" % (count, effects.beam_count))

    effects.place_coronas(lighting)
    effects.place_emitters(scct_fx.load(os.path.join(RENDER, "fx.json")))
    effects.place_projectors(scct_fx.load(os.path.join(RENDER, "projectors.json")))
    effects.place_rain(scct_fx.load(os.path.join(CONTENT_ROOT, "svmap", MAP, "sim", "rain.json")))
    effects.place_props(scct_fx.load(os.path.join(CONTENT_ROOT, "svmap", MAP, "sim", "gameplay.json")))


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
        # The moon keeps the directional light class's own -46 degree pitch under the pitch map_skies.json gives
        # (so it stands higher than that file says): the skies were tuned and reviewed by eye that way.
        moon = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 4000), unreal.Rotator(roll=0, pitch=moon_look["pitch"], yaw=moon_look["yaw"]),
                     class_turn=True)
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
    fog_look = sky_look.get("fog", {"density": 0.006, "colour": [0.02, 0.03, 0.05]})
    effects.place_zone_fog(lighting, whole_map_fog="fog" in sky_look)
    try_set(fog_component, "fog_density", fog_look["density"])
    try_set(fog_component, "fog_height_falloff", 0.02)
    try_set(fog_component, "fog_inscattering_luminance", unreal.LinearColor(fog_look["colour"][0], fog_look["colour"][1], fog_look["colour"][2], 1.0))
    try_set(fog_component, "enable_volumetric_fog", True)
    try_set(fog_component, "volumetric_fog_scattering_distribution", 0.6)
    try_set(fog_component, "volumetric_fog_extinction_scale", 1.2)

    post = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    post.set_folder_path("Atmosphere")
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in [
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        # In EV100 (the project extends the luminance range). The game sets these three over this volume for
        # every map, from its own settings (Source/OpenSCCT/Play/ScctExposure.cpp, scct.Exposure.*): the eye opens
        # no further than a dim lamp-lit room needs, so a dark place stays dark, as it was in the original.
        ("auto_exposure_min_brightness", -3.0),
        ("auto_exposure_max_brightness", 4.0),
        ("auto_exposure_bias", -0.75),
        # The eye is quick: a match starts on the right exposure within a second, and walking out of a dark
        # corridor does not leave the yard white for long.
        ("auto_exposure_speed_up", 5.0),
        ("auto_exposure_speed_down", 2.5),
        # Local exposure: a lit doorway seen from a dark room keeps its detail and the dark room keeps its shapes,
        # instead of one burning out or the other going black (a floodlit wall beside a man in a black suit).
        ("local_exposure_highlight_contrast_scale", 0.6),
        ("local_exposure_shadow_contrast_scale", 0.6),
        ("bloom_intensity", 0.6),
        ("bloom_threshold", -1.0),
        ("vignette_intensity", 0.45),
        ("film_grain_intensity", 0.12),
        ("scene_fringe_intensity", 0.1),
        ("lens_flare_intensity", 0.4),
        ("ambient_occlusion_intensity", 0.6),
        # A trace of blur on a fast turn and nothing to see at a walk: at 0.35 the walls smeared while walking.
        ("motion_blur_amount", MOTION_BLUR),
        ("motion_blur_max", MOTION_BLUR_MOST),
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
