"""The effects of a converted OpenSCCT map, placed into the level import_map.py is building: particle emitters,
(and, further down, the other things the original drew that are not plain surfaces and lights).

Runs inside the editor's Python as part of import_map.py. The assets it places are built by import_effects.py
(scct_effects.py, scct_niagara.py). The original engine's rules it follows are in OpenSCCT's
spec/native/emitters.md and docs/RENDERING.md; where this differs on purpose (lit smoke, soft edges) it says so.
"""
import hashlib
import json
import math
import os
import re

import unreal

import scct_effects
import scct_niagara

materials_lib = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary

# The original's particle draw styles (ParticleEmitter.DrawStyle).
REGULAR, ALPHA_BLEND, MODULATED, TRANSLUCENT, ALPHA_MODULATE, DARKEN, BRIGHTEN = range(7)
# The original's UseDirectionAs values that lay a sprite along its motion, and in a fixed plane.
ALIGNED_SPRITES = (1, 2, 5, 6)   # Up, Right, UpAndNormal, RightAndNormal
FLAT_SPRITES = (3, 4)            # Forward, Normal

# The original's light effect and light type numbers that matter here.
ECHELON_SPOT = 22                     # LE_ESpotShadow: a projector light (rectangle, picture, own shadow)
STEADY = 1
ANIMATED_LIGHT_TYPES = (2, 3, 4, 7)   # pulse, blink, flicker, subtle pulse
# Projector frame blends (Projector.FrameBufferBlendingOp).
PROJECT_MODULATE, PROJECT_ALPHA, PROJECT_ADD = 1, 2, 3

# The Actor class's defaults for a projector light, and the LightBrightness past which it gets no brighter.
ECHELON_DEFAULT_RANGE = 1000.0
ECHELON_DEFAULT_SPOT = 250.0
ECHELON_BRIGHTNESS_MOST = 510.0
ECHELON_FALLOFF = 2.0     # the engine fades as (1 - (d / R)^2) ^ this
DEFAULT_SPOT_PICTURE = "echelon_spot_bw"
# How much of a beam light's light the air scatters, per square root of the original's beam alpha.
BEAM_SCATTER = 4.0
BEAM_SCATTER_MOST = 16.0
CORONA_BRIGHTNESS = 0.8
# A caustics light: its strength in import_map's light units, and how far its (mostly dark) picture is lifted.
CAUSTIC_INTENSITY = 4.0
CAUSTIC_GAIN = 2.5

# Rain: drops per second over each square metre of a rain volume, their speed and half sizes (world units).
RAIN_PER_SQUARE_METRE = 14.0
RAIN_SPEED = (1300.0, 1700.0)
RAIN_HALF_SIZE = (0.5, 22.0)
RAIN_MOST_PER_SECOND = 5000.0
WET_BELOW_VOLUME = 64.0     # how far under a rain volume's floor its wet ground still reaches
# Zone fog: the thickest a fogged zone gets across its whole width (optical depth), so rooms stay playable.
FOG_DEPTH_MOST = 2.0
DRAW_MESH = 2      # Actor.DrawType DT_Mesh: a skeletal mesh

# Light given off by additive sprites (sparks, flames, bubbles), in the units of import_map's emissive surfaces.
GLOW_BRIGHTNESS = 1.5
FOREVER = 1.0e9


def key(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


def pick(props, name, default):
    value = props.get(name)
    return default if value is None else value


def span(props, name, default):
    """A Range property as (min, max)."""
    value = props.get(name)
    return (float(value["Min"]), float(value["Max"])) if value else default


def span3(props, name, default):
    """A RangeVector property as ((min x, y, z), (max x, y, z))."""
    value = props.get(name)
    if not value:
        return default
    low, high = [], []
    for index, axis in enumerate("XYZ"):
        part = value.get(axis)
        low.append(float(part["Min"]) if part else default[0][index])
        high.append(float(part["Max"]) if part else default[1][index])
    return tuple(low), tuple(high)


def vector(value, default=(0.0, 0.0, 0.0)):
    return (float(value["X"]), float(value["Y"]), float(value["Z"])) if value else default


def four_keys(points, start):
    """A curve of the original (key frames over the share of a particle's life, straight lines between them,
    `start` before the first) as exactly four keys: ([times], [values]). Longer curves keep their first and last
    keys and the two in between that matter most."""
    keys = [(0.0, start)]
    for time, value in sorted(points, key=lambda point: point[0]):
        if time <= 0.0:
            keys[0] = (0.0, value)    # a key at the very start gives its own value
        else:
            keys.append((min(time, 1.0), value))
    while len(keys) > 4:
        # Drop the inner key whose removal changes the curve least (smallest jump to the line through its neighbours).
        def cost(index):
            (t0, v0), (t1, v1), (t2, v2) = keys[index - 1], keys[index], keys[index + 1]
            share = (t1 - t0) / max(t2 - t0, 1e-6)
            flat = lambda v: v if isinstance(v, (int, float)) else sum(v) / len(v)  # noqa: E731
            return abs(flat(v1) - (flat(v0) + (flat(v2) - flat(v0)) * share))
        keys.pop(min(range(1, len(keys) - 1), key=cost))
    while len(keys) < 4:
        keys.append((1.0, keys[-1][1]))
    return [time for time, _ in keys], [value for _, value in keys]


def sprite_parameters(props, turn_back):
    """The user parameters of NS_ScctSprites for one sub-emitter of the original (spec/native/emitters.md).
    `turn_back` takes a world direction into the placed actor's frame. Returns (parameters, starts active)."""
    life = span(props, "LifetimeRange", (4.0, 4.0))
    average_life = max((life[0] + life[1]) * 0.5, 0.05)
    slots = float(pick(props, "MaxParticles", 10))
    steady = slots / average_life     # every slot in use, each respawning as its particle dies
    window = loop = FOREVER
    if pick(props, "RespawnDeadParticles", True):
        rate = steady
    elif pick(props, "ParticlesPerSecond", 0.0) > 0.0:
        rate = float(props["ParticlesPerSecond"])
    else:
        # A burst: every slot is used once. A triggered burst spawns what its trigger asks for instead.
        triggered = span(props, "SpawnOnTriggerRange", (0.0, 0.0))
        count = (triggered[0] + triggered[1]) * 0.5 or slots
        rate = pick(props, "SpawnOnTriggerPPS", 0.0) if triggered[1] > 0.0 else 0.0
        if rate <= 0.0:
            rate = steady if pick(props, "AutomaticInitialSpawning", True) else float(pick(props, "InitialParticlesPerSecond", 0.0)) or steady
        window = max(count / rate, 0.02)
        if pick(props, "AutoReset", False):
            pause = span(props, "AutoResetTimeRange", (0.0, 0.0))
            loop = window + max(life) + (pause[0] + pause[1]) * 0.5

    offset = vector(props.get("StartLocationOffset"))
    box = span3(props, "StartLocationRange", ((0, 0, 0), (0, 0, 0)))
    velocity = span3(props, "StartVelocityRange", ((0, 0, 0), (0, 0, 0)))
    radial = span(props, "StartVelocityRadialRange", (0.0, 0.0)) if props.get("GetVelocityDirectionFrom") == 3 else (0.0, 0.0)
    loss = span3(props, "VelocityLossRange", ((0, 0, 0), (0, 0, 0)))

    direction = props.get("UseDirectionAs", 0)
    kind = 1 if direction in ALIGNED_SPRITES else 2 if direction in FLAT_SPRITES else 0
    size = span3(props, "StartSizeRange", ((100, 100, 100), (100, 100, 100)))
    # A camera-facing sprite reads only X (the engine forces it square). Others are X across and Y along; the
    # "Right" kinds have the two swapped (X lies along the motion).
    uniform = bool(pick(props, "UniformSize", False)) or kind == 0
    across, along = (1, 0) if direction in (2, 6) else (0, 1)
    size_low, size_high = (size[0][across], size[0][along]), (size[1][across], size[1][along])
    if uniform:
        size_low, size_high = (size[0][0], size[0][0]), (size[1][0], size[1][0])

    size_times, size_values, size_decay = [0, 1, 1, 1], [1, 1, 1, 1], 0.0
    if pick(props, "UseSizeScale", False):
        if pick(props, "UseRegularSizeScale", True):
            size_decay = 1.0
        else:
            size_times, size_values = four_keys([(p["RelativeTime"], p["RelativeSize"]) for p in props.get("SizeScale", [])], 1.0)

    style = pick(props, "DrawStyle", TRANSLUCENT)
    colour_times, colours = [0, 1, 1, 1], [(1, 1, 1, 1)] * 4
    if pick(props, "UseColorScale", False):
        points = [(p["RelativeTime"], tuple(p["Color"][c] / 255.0 for c in "RGBA")) for p in props.get("ColorScale", [])]
        colour_times, colours = four_keys(points, (1.0, 1.0, 1.0, 1.0))
        # Display values to linear; the curve's alpha counts only for alpha-blended sprites.
        colours = [(c[0] ** 2.2, c[1] ** 2.2, c[2] ** 2.2, c[3] if style == ALPHA_BLEND else 1.0) for c in colours]
    multiplier = span3(props, "ColorMultiplierRange", ((1, 1, 1), (1, 1, 1)))
    tint = tuple(((low + high) * 0.5) ** 2.2 for low, high in zip(*multiplier)) + (1.0,)

    spin = (0.0, 0.0, 0.0, 0.0)
    if pick(props, "SpinParticles", False):
        start = span3(props, "StartSpinRange", ((0, 0, 0), (0, 0, 0)))
        turns = span3(props, "SpinsPerSecondRange", ((0, 0, 0), (0, 0, 0)))
        spin = (start[0][0], start[1][0], turns[0][0], turns[1][0])
    cells = max(1, pick(props, "TextureUSubdivisions", 1)) * max(1, pick(props, "TextureVSubdivisions", 1))

    parameters = {
        "Rate": rate, "SpawnWindow": window, "LoopDuration": loop,
        "LifeMin": life[0], "LifeMax": life[1],
        "BoxMin": tuple(o + b for o, b in zip(offset, box[0])), "BoxMax": tuple(o + b for o, b in zip(offset, box[1])),
        "VelMin": velocity[0], "VelMax": velocity[1], "Radial": radial,
        "Accel": turn_back(vector(props.get("Acceleration"))),
        "VelLoss": tuple((low + high) * 0.5 for low, high in zip(*loss)),
        "SizeMin": size_low, "SizeMax": size_high, "SizeUniform": 1.0 if uniform else 0.0,
        "SizeTimes": size_times, "SizeValues": size_values, "SizeDecay": size_decay,
        "SizeRepeats": float(pick(props, "SizeScaleRepeats", 0.0)) + 1.0,
        "ColorTimes": colour_times, "Color0": colours[0], "Color1": colours[1], "Color2": colours[2], "Color3": colours[3],
        "ColorRepeats": float(pick(props, "ColorScaleRepeats", 0.0)) + 1.0, "Tint": tint,
        "FadeIn": float(pick(props, "FadeInEndTime", 0.0)) if pick(props, "FadeIn", False) else 0.0,
        "FadeOut": float(pick(props, "FadeOutStartTime", 0.0)) if pick(props, "FadeOut", False) else FOREVER,
        "Spin": spin, "Cells": (cells, 0.0 if pick(props, "UseRandomSubdivision", False) else 1.0),
        "Kind": float(kind), "Normal": vector(props.get("ProjectionNormal"), (0.0, 0.0, 1.0)),
    }
    return parameters, not pick(props, "Disabled", False)


def set_parameters(component, parameters):
    """Stores user parameter values on a placed Niagara component (they are saved with the level)."""
    for name, value in parameters.items():
        if isinstance(value, (int, float)):
            component.set_variable_float(name, float(value))
        elif len(value) == 2:
            component.set_variable_vec2(name, unreal.Vector2D(value[0], value[1]))
        elif len(value) == 3:
            component.set_variable_vec3(name, unreal.Vector(value[0], value[1], value[2]))
        elif name.startswith("Color") or name == "Tint":
            component.set_variable_linear_color(name, unreal.LinearColor(value[0], value[1], value[2], value[3]))
        else:
            component.set_variable_vec4(name, unreal.Vector4(value[0], value[1], value[2], value[3]))


class MapEffects:
    """Places a map's effects. `spawn(cls, location, rotation)` puts an actor into the level being built;
    `texture_of(path)` finds or imports a texture of the map by its file under render/; `import_files(files,
    destination)` imports source files."""

    def __init__(self, map_name, render, dest, spawn, texture_of, import_files, log):
        self.map, self.render, self.dest = map_name, render, dest
        self.spawn, self.texture_of, self.import_files, self.log = spawn, texture_of, import_files, log
        self.instances = {}
        beams = load(os.path.join(render, "lightbeams.json")).get("beams", [])
        self.beams = {beam["name"]: beam for beam in beams if beam.get("lightOn", True)}
        self.beam_count = 0

    # -----------------------------------------------------------------------------------------------------------
    # Materials
    # -----------------------------------------------------------------------------------------------------------

    def instance(self, name, parent, scalars=None, textures=None, vectors=None):
        """A material instance of one of the effect masters under <map>/Effects, made once per name."""
        if name in self.instances:
            return self.instances[name]
        folder = self.dest + "/Effects"
        path = folder + "/" + name
        instance = unreal.load_asset(path) if editor_assets.does_asset_exist(path) else asset_tools.create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        clear = getattr(materials_lib, "clear_all_material_instance_parameters", None)
        if clear:
            clear(instance)
        materials_lib.set_material_instance_parent(instance, unreal.load_asset(parent))
        for parameter, value in (scalars or {}).items():
            materials_lib.set_material_instance_scalar_parameter_value(instance, parameter, float(value))
        for parameter, texture in (textures or {}).items():
            if texture:
                materials_lib.set_material_instance_texture_parameter_value(instance, parameter, texture)
        for parameter, value in (vectors or {}).items():
            materials_lib.set_material_instance_vector_parameter_value(instance, parameter, unreal.LinearColor(*value))
        materials_lib.update_material_instance(instance)
        self.instances[name] = instance
        return instance

    # -----------------------------------------------------------------------------------------------------------
    # Emitters
    # -----------------------------------------------------------------------------------------------------------

    def particle_material(self, texture_file, style, columns, rows):
        """The material of one sprite emitter. Alpha-blended sprites (smoke, steam, spray) are lit by the room;
        the additive styles glow. The original's darkening styles are rare and drawn as lit smoke too."""
        stem = os.path.splitext(os.path.basename(texture_file))[0] if texture_file else "none"
        glow = style in (TRANSLUCENT, BRIGHTEN, ALPHA_MODULATE, REGULAR)
        name = "MI_Fx_%s_%s_%dx%d" % (re.sub(r"[^A-Za-z0-9_]", "_", stem), "glow" if glow else "lit", columns, rows)
        texture = self.texture_of(texture_file) if texture_file else None
        scalars = {"Columns": columns, "Rows": rows}
        if glow:
            scalars["Brightness"] = GLOW_BRIGHTNESS
        return self.instance(name, scct_effects.PARTICLE_GLOW if glow else scct_effects.PARTICLE_LIT, scalars, {"Texture": texture})

    def place_emitters(self, document):
        """Every Emitter actor of the map: one Niagara component per sprite sub-emitter. Sub-emitters the
        original starts disabled (explosions, extinguishers, broken glass: they wait for a gameplay event) are
        placed switched off and tagged, for the game code to activate."""
        system = unreal.load_asset(scct_niagara.SPRITES)
        if not system:
            raise RuntimeError("effect assets are missing: run Tools/Import/Import-Effects.ps1 first")
        running = waiting = skipped = 0
        for name, emitter in sorted(document.get("emitters", {}).items()):
            actor_props = emitter.get("props", {})
            location = unreal.Vector(*vector(actor_props.get("Location")))
            turn = actor_props.get("Rotation") or {}
            actor_rotation = unreal.Rotator(roll=turn.get("Roll", 0) * 360.0 / 65536.0, pitch=turn.get("Pitch", 0) * 360.0 / 65536.0,
                                            yaw=turn.get("Yaw", 0) * 360.0 / 65536.0)
            for index, sub in enumerate(emitter.get("emitters", [])):
                props = sub.get("props", {})
                if not sub.get("class", "").endswith("SpriteEmitter") or pick(props, "MaxParticles", 10) <= 0:
                    skipped += 1    # mesh emitters (debris): not drawn yet
                    continue
                # The original turns start positions and velocities by the actor only when asked to
                # (UseRotationFrom = actor); acceleration is always in world axes. The placed actor carries that
                # turn, and the acceleration is taken into its frame.
                rotation = actor_rotation if props.get("UseRotationFrom") == 1 else unreal.Rotator(0, 0, 0)

                def turn_back(direction, rotation=rotation):
                    local = unreal.MathLibrary.less_less_vector_rotator(unreal.Vector(*direction), rotation)
                    return (local.x, local.y, local.z)

                parameters, active = sprite_parameters(props, turn_back)
                actor = self.spawn(unreal.NiagaraActor, location, rotation)
                label = "%s_%d" % (name, index)
                actor.set_actor_label(label)
                actor.set_folder_path("Effects/Emitters" if active else "Effects/Triggered")
                actor.tags = [unreal.Name("Scct:" + name), unreal.Name("ScctTag:" + str(actor_props.get("Tag", ""))),
                              unreal.Name("ScctEmitter:%d" % index)]
                if not pick(props, "TriggerDisabled", True):
                    # A trigger does not switch this one on and off: it fires its burst again each time
                    # (Play/ScctLevelStateSubsystem, TriggerEmitters).
                    actor.tags = list(actor.tags) + [unreal.Name("ScctEmitterBurst")]
                component = actor.niagara_component
                component.set_asset(system)
                set_parameters(component, parameters)
                material = self.particle_material(sub.get("texture"), pick(props, "DrawStyle", TRANSLUCENT),
                                                  max(1, pick(props, "TextureUSubdivisions", 1)), max(1, pick(props, "TextureVSubdivisions", 1)))
                component.set_variable_material("Material", material)
                if not active:
                    # As a property: the component's SetAutoActivate call does nothing on a component that is
                    # already registered with its world, and every disabled emitter then ran from the start.
                    component.set_editor_property("auto_activate", False)
                    waiting += 1
                else:
                    running += 1
        self.log("emitters: %d running, %d waiting for an event, %d mesh emitters not drawn" % (running, waiting, skipped))


    # -----------------------------------------------------------------------------------------------------------
    # Lights: projector lights, animated lights, beams
    # -----------------------------------------------------------------------------------------------------------

    def light_function(self, scalars, cookie=None):
        """The light function instance with these values, shared by every light that asks for the same ones:
        the engine keeps all the light functions in view in one small atlas (16 places unless
        r.LightFunctionAtlas.Size is raised), and a map with one instance per light ran out of places."""
        values = sorted((name, round(float(value), 4)) for name, value in scalars.items())
        text = repr((values, str(cookie.get_path_name()) if cookie else ""))
        return self.instance("MI_Light_" + hashlib.md5(text.encode()).hexdigest()[:10], scct_effects.LIGHT_FUNCTION,
                             scalars, {"Cookie": cookie})

    def dress_light(self, light, actor, per_brightness):
        """What a placed light needs beyond colour, strength and cone. `per_brightness` is import_map's light
        arriving per unit of the original's LightBrightness."""
        component = actor.light_component
        scalars, cookie = {}, None

        # ---- the original's projector lights (LE_ESpotShadow, "Echelon spots": floodlights, window and sun
        # patches, slide projectors). They are not cones: a rectangle SpotWidth x SpotHeight (half extents) at
        # EchelonRange, with a picture in it, fading in a straight line to nothing at the range, and their
        # brightness stops rising at 510 (docs/RENDERING.md, Spot shadow lights). Here: a cone around the
        # rectangle, cut down to it and given its picture by a light function. The original's rays start from a
        # window (its near plane), not a point; a spot light cannot do that, so close to the light the patch is
        # narrower than the original's.
        # Values a light does not set are the Actor class's defaults: range 1000, rectangle 250 x 250.
        if light.get("effect") == ECHELON_SPOT and light["light_type"] == "spot":
            width = light.get("spot_width") or ECHELON_DEFAULT_SPOT
            height = light.get("spot_height") or ECHELON_DEFAULT_SPOT
            reach = light.get("echelon_range") or ECHELON_DEFAULT_RANGE
            outer = max(5.0, min(80.0, math.degrees(math.atan(math.hypot(width, height) / reach))))
            across = math.tan(math.radians(outer)) * reach
            component.set_attenuation_radius(reach)
            component.set_outer_cone_angle(outer)
            component.set_inner_cone_angle(outer)     # the light function draws the edge
            # The original fades in a straight line to nothing at the range; this is the engine's nearest curve.
            component.set_editor_property("light_falloff_exponent", ECHELON_FALLOFF)
            # The original halves these lights' colour before its doubling light stage (brightness / 510, at
            # most 1), so at the same LightBrightness one gives half the light of an ordinary lamp.
            component.set_intensity(min(light["brightness"], ECHELON_BRIGHTNESS_MOST) * 0.5 * per_brightness)
            scalars.update({"RectU": across / width, "RectV": across / height})
            # A light that names the default spot texture gets the built-in soft spot like one that names none.
            picture = light.get("echelon_texture_uri") or ""
            if picture and not os.path.splitext(picture)[0].lower().endswith(DEFAULT_SPOT_PICTURE):
                cookie = self.texture_of(picture)
            if cookie:
                scalars["CookieWeight"] = 1.0
            else:
                scalars["Round"] = 1.0     # the soft round spot of M_ScctLightFunction
            actor.tags = list(actor.tags) + [unreal.Name("ScctEchelonSpot")]

        # ---- pulse, blink and flicker: the light function carries the rhythm, so it needs no game code. The
        # tag lets game code find these lights (their coronas follow the same rhythm in the original).
        kind = light.get("ltype", STEADY)
        if kind in ANIMATED_LIGHT_TYPES:
            scalars.update({"LightType": kind, "Rate": 35.0 / max(1.0, light.get("light_period", 32)),
                            "Phase": light.get("light_phase", 0) / 256.0 * 2.0 * math.pi})
            actor.tags = list(actor.tags) + [unreal.Name("ScctLightType:%d" % kind)]

        if scalars:
            component.set_editor_property("light_function_material", self.light_function(scalars, cookie))

        # ---- beams: the original drew a fan of see-through planes along some projector lights. With
        # volumetric fog a light draws its own shaft, shaped by its light function and cut by what stands in
        # it, so those lights scatter more light into the air instead. The strength follows the original's
        # rule (the square root of LightBeamInitialAlpha).
        beam = self.beams.get(light["name"])
        if beam:
            scatter = BEAM_SCATTER * math.sqrt(max(beam.get("alpha", 1), 1)) * beam.get("falloffScale", 1.0)
            component.set_editor_property("volumetric_scattering_intensity", min(scatter, BEAM_SCATTER_MOST))
            actor.tags = list(actor.tags) + [unreal.Name("ScctBeam")]
            self.beam_count += 1

    # -----------------------------------------------------------------------------------------------------------
    # Coronas
    # -----------------------------------------------------------------------------------------------------------

    def corona_card(self, name, location, texture_file, colour, least, most, scale, reach, lit, folder):
        texture = self.texture_of(texture_file) if texture_file else None
        if not texture:
            return False
        stem = os.path.splitext(os.path.basename(texture_file))[0]
        material = self.instance("MI_Corona_" + re.sub(r"[^A-Za-z0-9_]", "_", stem), scct_effects.CORONA,
                                 {"Brightness": CORONA_BRIGHTNESS}, {"Texture": texture})
        actor = self.spawn(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(0, 0, 0))
        actor.set_actor_label("Corona_" + name)
        actor.set_folder_path(folder)
        actor.tags = [unreal.Name("ScctCoronaOf:" + name)]
        component = actor.static_mesh_component
        component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
        component.set_material(0, material)
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_cast_shadow(False)
        component.set_mobility(unreal.ComponentMobility.MOVABLE)   # it follows a light the game may move
        for attribute in ("visible_in_ray_tracing", "affect_dynamic_indirect_lighting", "affect_distance_field_lighting"):
            try:
                component.set_editor_property(attribute, False)
            except Exception:  # pylint: disable=broad-except
                pass
        # The material puts the corners where they belong; the mesh only has to be large enough that the
        # engine does not cull the card while its halo is on screen (the plane is 100 units across).
        size = most * 2.0 / 100.0 * 1.5
        actor.set_actor_scale3d(unreal.Vector(size, size, size))
        for offset, value in enumerate(colour[:3]):
            component.set_default_custom_primitive_data_float(scct_effects.CORONA_COLOUR_SLOT + offset, value)
        component.set_default_custom_primitive_data_float(scct_effects.CORONA_MIN_SLOT, least)
        component.set_default_custom_primitive_data_float(scct_effects.CORONA_MAX_SLOT, most)
        component.set_default_custom_primitive_data_float(scct_effects.CORONA_SCALE_SLOT, scale)
        component.set_default_custom_primitive_data_float(scct_effects.CORONA_REACH_SLOT, reach)
        if not lit:
            actor.set_actor_hidden_in_game(True)    # its light is switched on by the game
        return True

    def place_coronas(self, lighting):
        """The halo of every light that has one (bCorona), and the lights that are drawn as a glowing sprite.
        Also for lights the importer does not place as lights: many of the original's coronas sit on lights
        that give no light at all."""
        count = 0
        for light in lighting.get("lights", []):
            lit = light.get("initially_on", True)
            corona = light.get("corona")
            if corona and corona.get("on", True) and corona.get("texture_uri"):
                # ScaleGlow is not a size: it is how fast the original fades a halo in and out.
                scale = corona.get("draw_scale", 1.0) * 512.0 * corona.get("scale3d", [1.0])[0]
                colour = [channel ** 2.2 for channel in corona.get("color", [1, 1, 1])]
                radius = corona.get("radius", 0.0)
                count += self.corona_card(light["name"], light["location"], corona["texture_uri"], colour,
                                          corona.get("min", 0.0), max(corona.get("max", 0.0), corona.get("min", 0.0), 1.0), scale,
                                          25.0 * (radius + 1.0) if radius > 0.0 else 0.0, lit, "Effects/Coronas")
            sprite = light.get("sprite")
            if sprite and sprite.get("blend") == "additive" and not light.get("hidden"):
                # One texel is one world unit times DrawScale; the card's size does not change with distance.
                half = sprite.get("width", 64) * sprite.get("draw_scale", 1.0) * 0.5
                count += self.corona_card(light["name"], light["location"], sprite["texture_uri"], [1.0, 1.0, 1.0],
                                          half, half, 0.0, 0.0, lit, "Effects/Coronas")
        self.log("coronas: %d" % count)

    # -----------------------------------------------------------------------------------------------------------
    # Projectors
    # -----------------------------------------------------------------------------------------------------------

    def place_projectors(self, document):
        """The map's texture projectors (docs/RENDERING.md, Projectors). A moving picture that multiplied the
        frame (pool caustics) becomes a real light with that picture as its light function: it then lights and
        shadows like the reflection off water it stands for. Everything else becomes a decal."""
        decals = lights = 0
        for projector in document.get("projectors", []):
            name = projector["name"]
            location = unreal.Vector(*projector["location"])
            rotation = rotator(projector["rotation"])
            forward = unreal.MathLibrary.greater_greater_vector_rotator(unreal.Vector(1, 0, 0), rotation)
            trace = max(projector.get("maxTrace", 100.0), 1.0)
            spread = math.tan(math.radians(projector["fov"] * 0.5)) if projector.get("fov", 0.0) > 1.0 else 0.0
            scale = projector.get("scale", [1.0, 1.0, 1.0])
            flipbook = projector.get("flipbook")
            texture = self.texture_of(flipbook["texture"] if flipbook else projector["texture"])
            frames = {"FlipColumns": flipbook["cols"], "FlipRows": flipbook["rows"], "FlipRate": flipbook["fps"],
                      "FlipFrames": flipbook["frames"]} if flipbook else {}
            tiles = projector.get("uvMatrix") or [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
            if not texture:
                continue

            if projector.get("fbBlend") == PROJECT_MODULATE and flipbook and spread > 0.0:
                actor = self.spawn(unreal.SpotLight, location, rotation)
                actor.set_actor_label(name)
                actor.set_folder_path("Effects/Projectors")
                actor.tags = [unreal.Name("Scct:" + name), unreal.Name("ScctCaustics")]
                component = actor.light_component
                component.set_mobility(unreal.ComponentMobility.MOVABLE)
                # A cone around the projector's square pyramid.
                outer = max(5.0, min(80.0, math.degrees(math.atan(spread * math.sqrt(2.0)))))
                component.set_outer_cone_angle(outer)
                component.set_inner_cone_angle(outer)
                component.set_editor_property("use_inverse_squared_falloff", False)
                component.set_editor_property("light_falloff_exponent", 1.0)
                component.set_attenuation_radius(trace * 1.5)
                component.set_intensity(CAUSTIC_INTENSITY)
                component.set_light_color(unreal.LinearColor(0.75, 0.95, 1.0, 1.0))
                component.set_cast_shadows(True)
                component.set_editor_property("volumetric_scattering_intensity", 0.3)
                ratio = math.tan(math.radians(outer)) / spread
                # Round: the patch fades out toward its edge instead of ending in the pyramid's square outline.
                scalars = dict(frames, RectU=ratio, RectV=ratio, Round=1.0, CookieWeight=1.0, CookieGain=CAUSTIC_GAIN,
                               TileU=tiles[0] or 1.0, TileV=tiles[3] or 1.0)
                component.set_editor_property("light_function_material", self.light_function(scalars, texture))
                lights += 1
                continue

            # The decal's box: from the projector to its trace distance, as wide as the picture is there.
            half_y = projector["size"][0] * abs(scale[1]) * 0.5 + trace * spread
            half_z = projector["size"][1] * abs(scale[2]) * 0.5 + trace * spread
            centre = location + forward * (trace * 0.5)
            actor = self.spawn(unreal.DecalActor, centre, rotation)
            actor.set_actor_label(name)
            actor.set_folder_path("Effects/Projectors")
            actor.tags = [unreal.Name("Scct:" + name)]
            added = projector.get("fbBlend") == PROJECT_ADD
            scalars = dict(frames, TileU=tiles[0] or 1.0, TileV=tiles[3] or 1.0,
                           FlipU=1.0 if scale[1] < 0.0 else 0.0,
                           OpacityFromBrightness=1.0 if added else 0.0)
            if added:
                # Added pictures are dark (they were summed onto the frame): lifted so they read as paint, and
                # glossy when they play (rain splashes on wet ground).
                scalars.update({"BrightnessGain": 4.0 if flipbook else 1.6, "Roughness": 0.2 if flipbook else 0.7,
                                "Emissive": 0.15 if flipbook else 0.0})
            stem = os.path.splitext(os.path.basename(projector["texture"]))[0]
            material = self.instance("MI_Projector_%s_%s" % (re.sub(r"[^A-Za-z0-9_]", "_", stem), "add" if added else "blend"),
                                     scct_effects.PROJECTOR, scalars, {"Texture": texture})
            decal = actor.decal
            decal.set_decal_material(material)
            decal.set_editor_property("decal_size", unreal.Vector(trace * 0.5, half_y, half_z))
            decals += 1
        self.log("projectors: %d decals, %d caustic lights" % (decals, lights))


    # -----------------------------------------------------------------------------------------------------------
    # Rain
    # -----------------------------------------------------------------------------------------------------------

    def place_rain(self, document):
        """The map's rain volumes (sim/rain.json; the simulation makes pawns standing in them wet). The original
        drew a few scrolling sheets near the camera; here every drop is a streak that falls through the volume
        and is lit by the lamps it passes."""
        volumes = document.get("volumes", [])
        if not volumes:
            return
        system = unreal.load_asset(scct_niagara.SPRITES)
        material = self.instance("MI_Fx_Rain", scct_effects.PARTICLE_LIT, {"Opacity": 0.32, "Emissive": 0.15, "SoftEdge": 4.0},
                                 {"Texture": unreal.load_asset(scct_effects.RAIN_STREAK)})
        for volume in volumes:
            scale = volume.get("scale", [1.0, 1.0, 1.0])
            half_x, half_y, height = (volume["size"][axis] * abs(scale[axis]) for axis in range(3))
            if min(half_x, half_y, height) <= 0.0:
                continue
            # The volume's box is (-X, -Y, 0) .. (X, Y, Z) from its location (docs/RENDERING.md, Rain volumes).
            speed = (RAIN_SPEED[0] + RAIN_SPEED[1]) * 0.5
            life = height / speed
            rate = min(RAIN_MOST_PER_SECOND, 4.0 * half_x * half_y / 10000.0 * RAIN_PER_SQUARE_METRE)
            actor = self.spawn(unreal.NiagaraActor, unreal.Vector(*volume["location"]), unreal.Rotator(0, 0, 0))
            actor.set_actor_label(volume["name"])
            actor.set_folder_path("Effects/Rain")
            actor.tags = [unreal.Name("Scct:" + volume["name"]), unreal.Name("ScctRain")]
            component = actor.niagara_component
            component.set_asset(system)
            set_parameters(component, {
                "Rate": rate, "SpawnWindow": FOREVER, "LoopDuration": FOREVER, "LifeMin": life * 0.95, "LifeMax": life * 1.05,
                "BoxMin": (-half_x, -half_y, height), "BoxMax": (half_x, half_y, height),
                # A little slant: rain never falls quite straight.
                "VelMin": (30.0, -10.0, -RAIN_SPEED[1]), "VelMax": (70.0, 10.0, -RAIN_SPEED[0]),
                "SizeMin": RAIN_HALF_SIZE, "SizeMax": (RAIN_HALF_SIZE[0] * 1.5, RAIN_HALF_SIZE[1] * 1.6), "SizeUniform": 0.0,
                "Kind": 1.0,
            })
            component.set_variable_material("Material", material)

            # Wet ground under the rain: a decal looking straight down through the volume, a little past its
            # floor (the box's floor is an estimate, docs/RENDERING.md). Turned to point down, the decal's
            # second and third axes are the world's Y and X.
            below = WET_BELOW_VOLUME
            wet = self.spawn(unreal.DecalActor, unreal.Vector(volume["location"][0], volume["location"][1],
                                                              volume["location"][2] + (height - below) * 0.5),
                             unreal.Rotator(roll=0, pitch=-90, yaw=0))
            wet.set_actor_label("Wet_" + volume["name"])
            wet.set_folder_path("Effects/Rain")
            wet.tags = [unreal.Name("ScctWetOf:" + volume["name"])]
            wet.decal.set_decal_material(unreal.load_asset(scct_effects.WET))
            wet.decal.set_editor_property("decal_size", unreal.Vector((height + below) * 0.5, half_y, half_x))
            # Under the projected rain splashes, which are decals too.
            wet.decal.set_editor_property("sort_order", -1)
        self.log("rain volumes: %d" % len(volumes))

    # -----------------------------------------------------------------------------------------------------------
    # Zone fog
    # -----------------------------------------------------------------------------------------------------------

    def place_zone_fog(self, lighting, whole_map_fog):
        """The original fogs single zones (an outdoor area, a steam room): distance fog from fog_start to
        fog_end in the zone's colour. Each such zone gets a local fog volume here. A zone's shape is not in the
        map file, so its box is taken from what is known to lie in it: its lights and the portals on its edge.
        Maps whose sky already fogs everything (map_skies.json), or where nearly every zone is fogged, are left
        to the level's own fog."""
        zones = [zone for zone in lighting.get("zones", []) if zone.get("fog") and not zone.get("sky")]
        if not zones or whole_map_fog or len(zones) > 0.6 * max(len(lighting.get("zones", [])), 1):
            return
        count = 0
        for zone in zones:
            points = [light["location"] for light in lighting.get("lights", []) if light.get("zone") == zone["name"]]
            for portal in lighting.get("fogPortals", []):
                if zone.get("zoneIndex") in (portal.get("front"), portal.get("back")):
                    points += portal["vertices"]
            if len(points) < 4:
                continue
            low = [min(point[axis] for point in points) for axis in range(3)]
            high = [max(point[axis] for point in points) for axis in range(3)]
            half = [max((high[axis] - low[axis]) * 0.5, 150.0) for axis in range(3)]
            centre = [(high[axis] + low[axis]) * 0.5 for axis in range(3)]
            # Fog reaches full strength at fog_end; across the zone's width that leaves this much light through.
            width = 2.0 * max(half[0], half[1])
            reach = max(zone.get("fog_end", 8000.0) - zone.get("fog_start", 0.0), 1.0)
            covered = min(max((width - zone.get("fog_start", 0.0)) / reach, 0.0), 0.95)
            depth = min(-math.log(1.0 - covered), FOG_DEPTH_MOST)
            if depth < 0.05:
                continue
            actor = self.spawn(unreal.LocalFogVolume, unreal.Vector(*centre), unreal.Rotator(0, 0, 0))
            actor.set_actor_label("Fog_" + zone["name"])
            actor.set_folder_path("Atmosphere/ZoneFog")
            actor.tags = [unreal.Name("ScctZone:" + zone["name"])]
            # The volume is a sphere of radius 500 at scale 1: stretched over the zone's box, a little beyond it
            # so the box's corners are inside.
            actor.set_actor_scale3d(unreal.Vector(*[extent * 1.3 / 500.0 for extent in half]))
            component = actor.get_component_by_class(unreal.LocalFogVolumeComponent)
            colour = [(channel / 255.0) ** 2.2 for channel in zone.get("fog_color", [128, 128, 128])[:3]]
            component.set_editor_property("radial_fog_extinction", depth * 0.5)   # the engine's unit: across a radius
            component.set_editor_property("height_fog_extinction", 0.0)
            component.set_editor_property("fog_albedo", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
            # The original's fog is a flat colour, lit by nothing: part of it is given as the fog's own glow so
            # a fogged zone does not turn black where no lamp reaches.
            component.set_editor_property("fog_emissive", unreal.LinearColor(colour[0] * 0.3, colour[1] * 0.3, colour[2] * 0.3, 1.0))
            count += 1
        self.log("zone fog volumes: %d of %d fogged zones" % (count, len(zones)))

    # -----------------------------------------------------------------------------------------------------------
    # Animated props
    # -----------------------------------------------------------------------------------------------------------

    def prop_assets(self, asset):
        """Imports a skeletal mesh of the map (render/chars/<package>/<mesh>.glb) once and returns
        (mesh, {sequence key: animation}, rotation origin, mesh scale), or None when the file is missing."""
        if asset in self.props:
            return self.props[asset]
        package, name = asset.split(".", 1)
        source = os.path.join(self.render, "chars", package, name + ".glb")
        result = None
        if os.path.exists(source):
            folder = "%s/Props/%s" % (self.dest, package)
            base = "%s/%s" % (folder, re.sub(r"[^A-Za-z0-9_]", "_", name))
            if not editor_assets.does_directory_exist(base):
                self.import_files([source], folder)
            mesh, sequences = None, {}
            for path in editor_assets.list_assets(base, recursive=True):
                loaded = unreal.load_asset(path)
                if isinstance(loaded, unreal.SkeletalMesh):
                    mesh = loaded
                elif isinstance(loaded, unreal.AnimSequence):
                    sequences[key(str(loaded.get_name()))] = loaded
            if mesh:
                document = glb_json(source)
                self.dress_prop(mesh, base, document)
                extras = document.get("extras", {}).get("svmap", {})
                result = (mesh, sequences, extras.get("rotOrigin", [0, 0, 0]), extras.get("meshScale", [1.0, 1.0, 1.0]))
        if not result:
            self.log("skeletal mesh not imported: " + asset)
        self.props[asset] = result
        return result

    def dress_prop(self, mesh, base, document):
        """Gives a prop's slots instances of the surface material (the importer's own materials are not built
        for skinned meshes in a game run), with the slot's original texture and cut-out where the file says so."""
        images = document.get("images", [])
        textures = document.get("textures", [])
        records = {}
        for material in document.get("materials", []):
            index = material.get("pbrMetallicRoughness", {}).get("baseColorTexture", {}).get("index")
            image = images[textures[index]["source"]] if index is not None else {}
            stem = os.path.splitext(os.path.basename(image.get("uri", image.get("name", ""))))[0]
            records[key(material.get("name", ""))] = (stem, material.get("alphaMode") == "MASK", bool(material.get("doubleSided")))
        found = {key(path.rsplit("/", 1)[1].split(".")[0]): path for path in editor_assets.list_assets(base, recursive=True)}
        surface = unreal.load_asset("/Game/OpenSCCT/Materials/M_ScctSurface")
        slots = mesh.get_editor_property("materials")
        for index in range(len(slots)):
            slot = slots[index]     # a copy: it is written back below
            imported = slot.get_editor_property("material_interface")
            if not imported or str(imported.get_name()).startswith("MI_Prop_"):
                continue
            stem, masked, two_sided = records.get(key(str(imported.get_name())), ("", False, False))
            name = "MI_Prop_" + re.sub(r"[^A-Za-z0-9_]", "_", str(imported.get_name()))
            path = base + "/Materials/" + name
            instance = unreal.load_asset(path) if editor_assets.does_asset_exist(path) else asset_tools.create_asset(
                name, base + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            materials_lib.set_material_instance_parent(instance, surface)
            texture = unreal.load_asset(found[key(stem)]) if key(stem) in found else None
            if texture:
                materials_lib.set_material_instance_texture_parameter_value(instance, "BaseColor", texture)
            materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessDark", 0.85)
            materials_lib.set_material_instance_scalar_parameter_value(instance, "RoughnessBright", 0.55)
            overrides = instance.get_editor_property("base_property_overrides")
            overrides.set_editor_property("override_blend_mode", masked)
            if masked:
                overrides.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            overrides.set_editor_property("override_two_sided", two_sided)
            overrides.set_editor_property("two_sided", two_sided)
            instance.set_editor_property("base_property_overrides", overrides)
            materials_lib.update_material_instance(instance)
            slot.set_editor_property("material_interface", instance)
            slots[index] = slot
        mesh.set_editor_property("materials", slots)
        editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)

    def place_props(self, gameplay):
        """The map's skeletal mesh actors (SAnimatedMesh: trees, fans, radar dishes, cranes, walls that can be
        blown up; SFish_Big: sharks, dolphins and rays). Those the original starts playing loop their first
        sequence; the rest stand in its first frame until the game plays them. Hidden ones (the broken state of
        something, an animation waiting for its event) are placed hidden. The simulation moves and plays them
        once game code drives these actors: they are tagged Scct:<actor id> like everything else."""
        self.props = {}
        placed = hidden = missing = 0
        for record in gameplay.get("actors", []):
            props = record.get("props", {})
            mesh_asset = (props.get("Mesh") or {}).get("$asset") if isinstance(props.get("Mesh"), dict) else None
            if not mesh_asset or props.get("DrawType") != DRAW_MESH or "." not in mesh_asset:
                continue
            assets = self.prop_assets(mesh_asset)
            if not assets:
                missing += 1
                continue
            mesh, sequences, origin, mesh_scale = assets
            turn = props.get("Rotation") or {}
            # The mesh's own rotation origin first, then the actor's rotation.
            rotation = unreal.MathLibrary.compose_rotators(
                rotator(origin), rotator([turn.get("Pitch", 0), turn.get("Yaw", 0), turn.get("Roll", 0)]))
            actor = self.spawn(unreal.SkeletalMeshActor, unreal.Vector(*vector(props.get("Location"))), rotation)
            draw = float(props.get("DrawScale", 1.0))
            draw3 = vector(props.get("DrawScale3D"), (1.0, 1.0, 1.0))
            actor.set_actor_scale3d(unreal.Vector(*[draw * draw3[axis] * mesh_scale[axis] for axis in range(3)]))
            actor.set_actor_label(record["id"])
            actor.set_folder_path("Geometry/Props")
            actor.tags = [unreal.Name("Scct:" + record["id"]), unreal.Name("ScctClass:" + record["class"].split(".")[-1]),
                          unreal.Name("ScctTag:" + str(props.get("Tag", "")))]
            component = actor.skeletal_mesh_component
            component.set_skinned_asset_and_update(mesh)
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)   # the simulation owns collision
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
            names = props.get("Animations") or []
            sequence = sequences.get(key(names[0])) if names else None
            if not sequence and len(sequences) == 1:
                sequence = next(iter(sequences.values()))
            if not sequence and names:
                # The importer puts the mesh's name before the sequence's.
                sequence = next((asset for name, asset in sequences.items() if name.endswith(key(names[0]))), None)
            if sequence:
                # Fish have no sequence list: the simulation swims them; until then they swim on the spot.
                playing = bool(props.get("bAutoStart")) or not names
                component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                data = component.get_editor_property("animation_data")
                data.set_editor_property("anim_to_play", sequence)
                data.set_editor_property("saved_looping", playing)
                data.set_editor_property("saved_playing", playing)
                data.set_editor_property("saved_position", 0.0)
                component.set_editor_property("animation_data", data)
            if props.get("bHidden"):
                actor.set_actor_hidden_in_game(True)
                hidden += 1
            placed += 1
        if placed or missing:
            self.log("skeletal props: %d placed (%d of them hidden), %d without a mesh file" % (placed, hidden, missing))


def glb_json(path):
    """The JSON part of a .glb file."""
    import struct
    with open(path, "rb") as file:
        file.read(12)
        length, _ = struct.unpack("<I4s", file.read(8))
        return json.loads(file.read(length))


def rotator(units):
    """[pitch, yaw, roll] in the original's 65536-per-turn units to an Unreal rotator."""
    turn = 360.0 / 65536.0
    return unreal.Rotator(roll=units[2] * turn, pitch=units[0] * turn, yaw=units[1] * turn)


def instance_tags(instance):
    """What game code will need to know about a placed mesh, as actor tags.

    The heat and electromagnetic values (what the thermal and EMF goggles draw): ScctHeat, ScctHeatOpacity,
    ScctEmf, ScctEmfOpacity, and ScctEmfOn when the device is running.
    ScctSoftBody:<patch or strip> marks the original's cloth (flags, awnings, strip curtains): placed at rest,
    waiting for a cloth simulation."""
    tags = []
    vision = instance.get("vision")
    if vision:
        tags += ["ScctHeat:%.3f" % vision.get("heat", 0.0), "ScctHeatOpacity:%.3f" % vision.get("heatOpacity", 1.0),
                 "ScctEmf:%.3f" % vision.get("emf", 0.0), "ScctEmfOpacity:%.3f" % vision.get("emfOpacity", 1.0)]
        if vision.get("emfOn"):
            tags.append("ScctEmfOn")
    soft = instance.get("softBody")
    if soft:
        tags.append("ScctSoftBody:" + str(soft.get("kind", "patch")))
    return [unreal.Name(tag) for tag in tags]


def load(path):
    return json.load(open(path)) if os.path.exists(path) else {}


__all__ = ["MapEffects", "load", "math"]
