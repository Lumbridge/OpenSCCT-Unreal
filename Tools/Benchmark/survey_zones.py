"""Sweep every source zone with a rotating camera while the Story simulation runs."""
import json
import os
from pathlib import Path
import time
import traceback

import unreal

config = json.loads(os.environ["SCCT_SURVEY_JSON"])
key = config["map"]
world = unreal.find_object(None, f"/Game/Imported/Maps/{key}/{key}.{key}")
output = Path(config["output"])
lighting = Path(config["content"]) / "svmap" / key / "render/lighting.json"
zones = [z for z in json.loads(lighting.read_text())["zones"] if z["class"] != "SkyZoneInfo"]
if not zones:
    raise ValueError(f"{key} contains no survey zones")

duration = 2.0
started = 0.0
variants = ["baseline"]
if config.get("diagnose"):
    # Repeat the same geographically distributed views for each independent change.
    zones = zones[::max(1, len(zones) // 6)][:6]
    variants += ["taa", "conventional_lights", "buffered_queries", "half_resolution", "repeat"]
index = -1
variant_index = 0
camera = None
phase = "start"
due = time.monotonic() + 8
samples = []
original = {}


def command(line):
    unreal.SystemLibrary.execute_console_command(world, line)


def set_variant():
    variant = variants[variant_index]
    options = dict(original)
    changes = {
        "taa": {"r.AntiAliasingMethod": 2},
        "conventional_lights": {"r.MegaLights.Allowed": 0},
        "buffered_queries": {"r.NumBufferedOcclusionQueries": 4},
        "half_resolution": {"r.ScreenPercentage": 50},
    }
    options.update(changes.get(variant, {}))
    for name, value in options.items():
        command(f"{name} {value}")
        actual = unreal.SystemLibrary.get_console_variable_float_value(name)
        if abs(actual - value) > 0.01:
            raise RuntimeError(f"{name}: requested {value}, got {actual}")
    unreal.log(f"SURVEY {variant} {options}")


def advance():
    global due, phase, camera, index, variant_index, started
    now = time.monotonic()
    if now < due:
        if phase == "sweep":
            camera.set_actor_rotation(unreal.Rotator(pitch=-4, yaw=(now - started) * 360 / duration, roll=0), True)
            samples[-1]["frames"] += 1
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if phase == "start":
        for line in ("Set Graphics.Quality Epic", "Set Graphics.RenderScale 100",
                     "Set Graphics.AutoScaleFps 0", "Set Display.VSync false", "Set Performance.MaxFps unlimited",
                     "r.DynamicRes.OperationMode 0", "r.GPUCsvStatsEnabled 1", "CsvCategory ScctSession enable",
                     "EnableCheats", "summon CameraActor"):
            command(line)
        camera = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)[-1]
        camera.camera_component.set_field_of_view(110)
        pc.set_view_target_with_blend(camera)
        for name in ("r.AntiAliasingMethod", "r.MegaLights.Allowed", "r.NumBufferedOcclusionQueries", "r.ScreenPercentage"):
            getter = (unreal.SystemLibrary.get_console_variable_float_value if name == "r.ScreenPercentage"
                      else unreal.SystemLibrary.get_console_variable_int_value)
            original[name] = getter(name)
        viewport = list(pc.get_viewport_size())
        if viewport != config.get("viewport", viewport):
            raise ValueError(f"Incorrect survey resolution: {viewport}")
        unreal.log(f"SURVEY viewport={viewport}, settings={original}")
        phase = "configure"
    if phase == "configure":
        set_variant()
        phase = "capture"
        due = now + 3
        return
    variant = variants[variant_index]
    if phase == "capture":
        command(f"CSVProfile STARTFILE={variant}.csv")
        command("CSVProfile START")
        phase = "next"
    if phase == "sweep":
        samples[-1]["seconds"] = now - started
        phase = "next"
    if phase == "next":
        index += 1
        if index < len(zones):
            zone = zones[index]
            camera.set_actor_location_and_rotation(
                unreal.Vector(*zone["location"]), unreal.Rotator(pitch=-4, yaw=0, roll=0), False, True
            )
            samples.append(dict(id=zone["name"], name=zone.get("location_name"),
                                position=zone["location"], frames=1))
            started = now
            due = now + duration
            phase = "sweep"
            return
        command("CSVProfile STOP")
        (output / f"{variant}.json").write_text(json.dumps(samples, indent=2))
        samples.clear()
        index = -1
        phase = "finalize"
        due = now + 2
        return
    if phase == "finalize":
        csv_path = output / "profile/Saved/Profiling/CSV" / (variant + ".csv")
        if not csv_path.exists() or csv_path.stat().st_size == 0:
            due = now + 0.25
            return
        variant_index += 1
        if variant_index == len(variants):
            unreal.unregister_slate_post_tick_callback(handle)
            command("quit")
        else:
            phase = "configure"


def tick(delta):
    try:
        advance()
    except Exception:
        unreal.log_error(traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle)
        command("quit")


handle = unreal.register_slate_post_tick_callback(tick)
