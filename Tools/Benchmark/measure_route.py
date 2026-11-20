"""Editor-side capture for benchmark_game.py."""

import json
import os
from pathlib import Path
import time

import unreal


config = json.loads(os.environ["SCCT_BENCHMARK"])
log_path = Path(config["log"])
world = unreal.find_object(None, config["world"])
if world is None:
    raise RuntimeError(f"Could not find gameplay world {config['world']}")

groups = (
    "ViewDistance", "AntiAliasing", "Shadow", "GlobalIllumination", "Reflection",
    "PostProcess", "Texture", "Effects", "Foliage", "Shading", "Landscape",
)


def command(text):
    unreal.SystemLibrary.execute_console_command(world, text)


def effective_settings():
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    settings = unreal.GameUserSettings.get_game_user_settings()
    get_int = unreal.SystemLibrary.get_console_variable_int_value
    get_float = unreal.SystemLibrary.get_console_variable_float_value
    levels = {group: get_int(f"sg.{group}Quality") for group in groups}
    presets = ("Low", "Medium", "High", "Epic", "Cinematic")
    tier = levels["ViewDistance"]
    preset = presets[tier] if set(levels.values()) == {tier} and 0 <= tier < len(presets) else "Custom"
    return {
        "viewport": list(controller.get_viewport_size()),
        "preset": preset,
        "groups": levels,
        "scale_percent": get_float("r.ScreenPercentage"),
        "dynamic_resolution": settings.is_dynamic_resolution_enabled(),
        "dynamic_resolution_mode": get_int("r.DynamicRes.OperationMode"),
        "fps_limit": settings.get_frame_rate_limit(),
        "max_fps_cvar": get_float("t.MaxFPS"),
        "vsync": get_int("r.VSync"),
        "clustered_lights": get_int("r.UseClusteredDeferredShading_ToBeRemoved"),
        "anti_aliasing_method": get_int("r.AntiAliasingMethod"),
    }


def prepare():
    for text in (
        "CsvCategory ScctSession enable",
        f"Set Graphics.Quality {config['quality']}",
        "Set Graphics.AutoScaleFps 0",
        "Set Graphics.RenderScale 100",
        f"Set Graphics.AntiAliasing {config['aa']}",
        "Set Performance.MaxFps unlimited",
        "Set Display.VSync false",
        "r.DynamicRes.OperationMode 0",
        "r.GPUCsvStatsEnabled 1",
    ):
        command(text)
    if config.get("clustered"):
        command("r.UseClusteredDeferredShading_ToBeRemoved 1")


def record_settings(label):
    snapshot = effective_settings()
    unreal.log("SCCT_BENCHMARK " + label + " " + json.dumps(snapshot, sort_keys=True))


phase = "waiting"
next_finalize_check = 0.0
image_index = 0
image_due = 0.0
motion_start = None
motion_delays = (0.5, 1.15, 1.8, 2.5)
cluster_images = ((0, "unclustered"), (1, "clustered"), (0, "unclustered_repeat"))


def set_image_stage():
    value, _ = cluster_images[image_index]
    command(f"r.UseClusteredDeferredShading_ToBeRemoved {value}")


def tick(_delta):
    global phase, next_finalize_check, image_index, image_due, motion_start
    if phase == "motion":
        controller = unreal.GameplayStatics.get_player_controller(world, 0)
        step = controller.get_autoplay_progress()
        if step == config["route_steps"] + 2:
            now = time.monotonic()
            if motion_start is None:
                motion_start = now
            if image_index < len(motion_delays) and now - motion_start >= motion_delays[image_index]:
                path = (log_path.parent / f"pan_{image_index}.png").as_posix()
                command(f'Shot -nosuffix filename="{path}"')
                image_index += 1
        elif step >= config["route_steps"] + 4:
            phase = "done"
            command("quit")
        return
    if phase == "images":
        if time.monotonic() < image_due:
            return
        value, label = cluster_images[image_index]
        assert unreal.SystemLibrary.get_console_variable_int_value("r.UseClusteredDeferredShading_ToBeRemoved") == value
        image_path = (log_path.parent / (label + ".png")).as_posix()
        command(f'Shot -nosuffix filename="{image_path}"')
        phase = "image_saved"
        return
    if phase == "image_saved":
        _, label = cluster_images[image_index]
        if not (log_path.parent / (label + ".png")).is_file():
            return
        image_index += 1
        if image_index == len(cluster_images):
            phase = "done"
            command("quit")
        else:
            set_image_stage()
            phase = "images"
            image_due = time.monotonic() + 1.5
        return
    if phase == "done":
        return
    if phase == "finalizing":
        # Only touch the profiling log after measurement has stopped.
        now = time.monotonic()
        if now >= next_finalize_check:
            next_finalize_check = now + 0.2
            with log_path.open("rb") as log:
                size = log.seek(0, 2)
                log.seek(max(0, size - 65536))
                if b"CSV finalize time" in log.read():
                    if config.get("motion"):
                        phase = "motion"
                    elif config.get("compare_cluster_images"):
                        unreal.GameplayStatics.set_game_paused(world, True)
                        set_image_stage()
                        phase = "images"
                        image_due = now + 1.5
                    else:
                        phase = "done"
                        command("quit")
        return

    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if controller is None:
        return
    step = controller.get_autoplay_progress()
    if phase == "waiting" and step == 1:
        prepare()
        phase = "warming"
    elif phase == "warming" and step == 2:
        record_settings("start")
        command("CSVProfile STARTFILE=route.csv")
        command("CSVProfile START")
        phase = "capturing"
    elif phase == "capturing" and step == config["route_steps"] + 1:
        record_settings("end")
        command("CSVProfile STOP")
        phase = "finalizing"


unreal.register_slate_post_tick_callback(tick)
