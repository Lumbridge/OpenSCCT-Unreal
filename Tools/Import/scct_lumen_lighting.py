"""Prepare imported map fixtures and level post processing for OpenSCCT's live Lumen lighting path."""
import json
import math
from pathlib import Path
import sys

import unreal


ENABLED_MAPS = {"MounE", "AquaE"}
WORLD_TAG = "ScctLumenLighting"
FIXTURE_TAG = "ScctLumenFixture"
RECOVERED_TAG = "ScctRecoveredFixture"
# These original light actors mark sources whose pool is visible in the released
# map but missing from the exported world-light list. Values multiply their
# source intensity after converting from the original falloff.
RECOVERED_FIXTURES = {
    "AquaE": {
        "light2221": 0.21,  # Spy Spawn ceiling neon above the paired pipes.
        "light457": 0.64,   # Tech Room ceiling neon behind the PIRATES doorway.
        "light12907": 0.31, # Pirates' ceiling fitting in front of the gallery.
        "light15411": 0.27, # Matching fitting at the far end of the exhibit.
        "light5050": 0.36,  # Self-lit hanging lantern above the wooden deck.
        "light3095": 0.19,  # The Blue Room has four matching FNAC gallery lamps.
        "light12201": 0.19,
        "light10513": 0.19,
        "light10510": 0.19,
        "light8999": 0.18,  # Ceiling lamp above the Blue Room seating.
        "light9569": 0.24,  # Greek Temple gallery spots beside the columns.
        "light9572": 0.24,
        "light9575": 0.24,
        "light34549": 0.22, # High ceiling source facing the temple deck.
    },
    "MounE": {
        "light6474": 0.02,   # Light above the MAIN ST sign.
        "light7842": 0.015,  # Warm light at the opposite end of the sewer bay.
    },
}


def update_tags(actor, add, remove):
    tags = [tag for tag in actor.tags if str(tag) not in remove]
    if add and add not in map(str, tags):
        tags.append(unreal.Name(add))
    if tags != list(actor.tags):
        actor.tags = tags


def is_environment_fixture(light):
    # Projectors use the engine's animated light function and original frustum. Triggered lamps
    # follow the simulation. Character-only and special-lit lamps have separate receiver sets.
    return (light.get("apply_static") and light.get("effect") != 22 and
            not any(light.get(flag) for flag in ("hidden", "special_lit", "dynamic", "triggered", "gameplay")))


def prepare(world, lights, map_folder):
    settings = world.get_world_settings()
    source = {light["name"].casefold(): light for light in lights}
    fixtures = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Light):
        for tag in actor.tags:
            prefix, _, name = str(tag).partition(":")
            if prefix == "Scct" and name.casefold() in source:
                if name.casefold() in fixtures:
                    raise RuntimeError(f"Two level lights bind to {name}")
                fixtures[name.casefold()] = actor
                break

    expected = {name for name, light in source.items() if is_environment_fixture(light)}
    map_name = Path(map_folder).name
    recovered = RECOVERED_FIXTURES.get(map_name, {})
    missing = sorted((expected | recovered.keys()) - fixtures.keys())
    if missing:
        raise RuntimeError("Level is missing source lighting fixtures: " + ", ".join(missing))

    for name, actor in fixtures.items():
        light = source[name]
        if map_name == "MounE" and name == "light246":
            from apply_hospital_room214_grid import apply_to_fixture
            apply_to_fixture(actor, light, map_folder)
        if name not in expected and name not in recovered:
            if RECOVERED_TAG in map(str, actor.tags):
                component = actor.light_component
                channels = unreal.LightingChannels()
                channels.set_editor_property("channel0", False)
                channels.set_editor_property("channel1", True)
                component.set_editor_property("lighting_channels", channels)
                component.set_editor_property("diffuse_scale", 1.0)
                component.set_cast_shadows(False)
                component.set_indirect_lighting_intensity(0.0)
            update_tags(actor, None, {FIXTURE_TAG, RECOVERED_TAG, "ScctNativeBakedLamp"})
            continue
        if name in recovered and (not light.get("apply_ingame") or light.get("apply_static")
                                  or any(light.get(k) for k in ("dynamic", "gameplay", "triggered", "hidden", "special_lit"))):
            raise RuntimeError(f"Recovered source fixture {name} has unexpected source lighting flags")

        component = actor.light_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        pitch, yaw, roll = (v * 360.0 / 65536 for v in light["rotation"])
        actor.set_actor_location_and_rotation(unreal.Vector(*light["location"]),
                                              unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll), False, True)
        radius = 25.0 * (light["radius"] + 1.0)
        component.set_attenuation_radius(radius)
        component.set_editor_property("use_inverse_squared_falloff", False)
        component.set_cast_shadows(True)
        channels = unreal.LightingChannels()
        channels.set_editor_property("channel0", True)
        channels.set_editor_property("channel1", False)
        channels.set_editor_property("channel2", False)
        component.set_editor_property("lighting_channels", channels)
        component.set_editor_property("diffuse_scale", recovered.get(name, 1.0))
        component.set_editor_property("specular_scale", 1.0)
        component.set_indirect_lighting_intensity(1.0)
        component.set_volumetric_scattering_intensity(0.0)
        # Source light values are gamma encoded. In the editor use their full native intensity.
        # The game solves the falloff against the original renderer's two lighting contours.
        colour = [max(channel, 0.0) ** 2.2 for channel in light["color"]]
        component.set_light_color(unreal.LinearColor(*colour, 1.0))
        component.set_intensity(light["brightness"] * (5.0 / 255.0))
        if isinstance(component, unreal.SpotLightComponent):
            cone = max(0.0, min(255.0, light["cone"])) / 256.0
            outer = math.degrees(1.15 * math.acos((1.0 - cone) ** 2)) * 0.5
            component.set_outer_cone_angle(outer)
            component.set_inner_cone_angle(outer * 0.45)
        component.set_visibility(bool(light.get("initially_on", True) and light.get("ltype", 1)))
        tag = RECOVERED_TAG if name in recovered else FIXTURE_TAG
        update_tags(actor, tag, {RECOVERED_TAG, FIXTURE_TAG, "ScctNativeBakedLamp"} - {tag})

    volumes = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PostProcessVolume)
               if actor.get_editor_property("unbound")]
    if not volumes:
        raise RuntimeError("Level is missing its unbound post-process volume")
    for volume in volumes:
        post = volume.get_editor_property("settings")
        post.set_editor_property("override_dynamic_global_illumination_method", True)
        post.set_editor_property("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.LUMEN)
        post.set_editor_property("override_reflection_method", True)
        post.set_editor_property("reflection_method", unreal.ReflectionMethod.LUMEN)
        volume.set_editor_property("settings", post)
    settings.set_editor_property("force_no_precomputed_lighting", True)
    unreal.ScctRuntimeMapActor.prepare_water_refraction(world, str(map_folder))
    from scct_receiver_lightmaps import prepare as prepare_receiver_lightmaps
    prepare_receiver_lightmaps(world, lights, map_folder)
    update_tags(settings, WORLD_TAG, {"ScctNativeLightmaps"})
    unreal.ScctRuntimeMapActor.detach_rebuilt_lightmaps(world)
    if recovered:
        unreal.log_warning("SCCT-IMPORT Recovered source fixtures: " +
                           ", ".join(f"{source[name]['name']} x{gain:.3g}" for name, gain in recovered.items()))
    return len(expected), len(fixtures)


def prepare_saved(content, key):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(f"/Game/Imported/Maps/{key}/{key}"):
        raise RuntimeError(f"Could not load {key}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    map_folder = Path(content) / "svmap" / key
    data = json.loads((map_folder / "render/lighting.json").read_text())
    count, total = prepare(world, data["lights"], map_folder)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {key}")
    unreal.log_warning(f"SCCT-IMPORT {key}: saved Lumen lighting with {count} environment fixtures among {total} bound lamps")


if __name__ == "__main__":
    prepare_saved(*sys.argv[1:3])
