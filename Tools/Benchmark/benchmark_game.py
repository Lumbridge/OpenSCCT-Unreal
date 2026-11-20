"""Benchmark real standalone matches without modifying the player's settings.

Example:
  python Tools/Benchmark/benchmark_game.py --resolution 2560x1440 --maps MounE AquaE MoonD --presets Low Epic
"""

import argparse
import csv
from contextlib import contextmanager
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile


PRESETS = ("Low", "Medium", "High", "Epic", "Cinematic")
ROUTE = (
    "wait 11", "forward 2", "turn 90 2 forward", "forward 2", "turn -90 2 right",
    "fire 0.5", "back 2", "turn 135 2 forward", "forward+jump 1", "turn 90 3 forward",
    "left 2", "turn -180 3 forward", "forward 2", "fire 1", "reload 1",
)
MOTION_TAIL = ("wait 3", "turn 160 3", "wait 1")
DEFAULT_PROJECT = Path(__file__).resolve().parents[2]


def parse_resolution(value):
    match = re.fullmatch(r"([0-9]+)[xX]([0-9]+)", value)
    if match:
        x, y = map(int, match.groups())
        if 640 <= x <= 7680 and 480 <= y <= 4320:
            return [x, y]
    raise argparse.ArgumentTypeError("use a viewport such as 2560x1440 (640x480 to 7680x4320)")


def select_level(args, map_id):
    level = f"/Game/Imported/Maps/{map_id}/{map_id}"
    cooked = (args.project / f"Content/Imported/Maps/{map_id}/{map_id}.umap").is_file()
    pack = args.content / "svmap" / map_id if args.content else None
    if pack and not (pack / "sim/gameplay.json").is_file():
        raise ValueError(f"{map_id}: no installed simulation data in {pack}")
    if args.map_source == "cooked" and not cooked:
        raise ValueError(f"{map_id}: no imported level in {args.project}")
    if cooked and args.map_source != "runtime":
        return "cooked", level, level
    if pack and not (pack / "render/world.glb").is_file():
        raise ValueError(f"{map_id}: no runtime geometry in {pack}")
    level = "/Game/OpenSCCT/RuntimeMap/L_RuntimeMap"
    return "runtime", level, level + f"?ScctMap={map_id}"


def switch(key, value):
    # Unreal's FParse expects quotes after the equals sign. All values are passed
    # straight to CreateProcess without a shell.
    value = str(value)
    if '"' in value or "\r" in value or "\n" in value:
        raise ValueError(f"invalid value for {key}")
    return f'-{key}="{value}"'


def read_metrics(path):
    samples = {key: [] for key in ("FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime")}
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = csv.DictReader(stream)
        if "FrameTime" not in (rows.fieldnames or ()):
            raise ValueError("CSV has no FrameTime column")
        samples.update({key: [] for key in rows.fieldnames if key.startswith("ScctSession/")})
        for row in rows:
            if row.get("FrameTime") == "FrameTime":
                # Unreal can repeat the header before its trailing metadata.
                break
            if not row.get("FrameTime"):
                continue
            for key, values in samples.items():
                value = row.get(key)
                if key == "FrameTime" or value:
                    milliseconds = float(value)
                    if not math.isfinite(milliseconds) or milliseconds < 0:
                        raise ValueError(f"CSV has invalid {key}")
                    values.append(milliseconds)
    frames = sorted(samples.pop("FrameTime"))
    if len(frames) < 120 or sum(frames) < 20_000 or frames[0] <= 0:
        raise ValueError("CSV does not cover a complete moving route")
    return {
        "frames": len(frames),
        "sample_seconds": round(sum(frames) / 1000, 3),
        "median_ms": round(statistics.median(frames), 3),
        "p99_ms": round(frames[math.ceil(len(frames) * 0.99) - 1], 3),
        "within_4_167_ms_percent": round(100 * sum(ms <= 1000 / 240 for ms in frames) / len(frames), 2),
        **{f"{key}_median_ms": round(statistics.median(values), 3) for key, values in samples.items() if values},
    }


def validate_capture(log_path, profile, map_id, source, viewport, quality, bots, motion=False):
    log = log_path.read_text(encoding="utf-8", errors="replace")
    if f"LogOpenSCCT: Starting {map_id} (Story) as merc with {bots} bots" not in log:
        raise ValueError("requested match did not start")
    if source == "runtime" and not re.search(rf"Runtime map {re.escape(map_id)}: built [1-9]\d* meshes", log):
        raise ValueError("requested runtime map geometry did not load")

    checkpoints = re.findall(
        r"LogOpenSCCT: Autoplay: (?:step (\d+) of \d+ '[^']+'|(finished)) \| pawn at \(([^)]+)\)", log,
    )
    steps = [int(step) for step, _, _ in checkpoints if step]
    if steps != list(range(1, len(ROUTE) + (len(MOTION_TAIL) if motion else 0) + 1)) or not checkpoints[-1][1]:
        raise ValueError("autoplay did not complete the requested route")
    positions = [tuple(map(float, position.split()[:2])) for _, _, position in checkpoints[:len(ROUTE) + 1]]
    distance = sum(math.dist(a, b) for a, b in zip(positions[1:], positions[2:]))
    if distance < 100:
        raise ValueError(f"autoplay moved only {distance:.1f} cm horizontally")

    captures = re.findall(r"SCCT_BENCHMARK (start|end) (\{[^\r\n]+\})", log)
    if [label for label, _ in captures] != ["start", "end"]:
        raise ValueError("missing start/end graphics settings in the capture")
    start, end = (json.loads(value) for _, value in captures)
    if start != end:
        raise ValueError("graphics settings changed during the capture")
    expected = {
        "viewport": viewport,
        "scale_percent": 100, "dynamic_resolution": False, "dynamic_resolution_mode": 0,
        "fps_limit": 0, "max_fps_cvar": 0, "vsync": 0,
    }
    actual_groups = start.get("groups", {})
    if (any(start.get(key) != value for key, value in expected.items())
            or len(actual_groups) != 11 or set(actual_groups.values()) != {quality}):
        raise ValueError(f"incorrect effective settings: {start}")

    path = profile / "Saved/Profiling/CSV/route.csv"
    if "CSV finalize time" not in log or not path.is_file() or path.stat().st_size == 0:
        raise ValueError("Unreal did not finalize a non-empty route.csv")
    return {
        "map": map_id, "map_source": source, "preset": PRESETS[quality], "renderer": "Unreal",
        "route_distance_cm": round(distance, 1), "settings": start,
        "csv": str(path), "log": str(log_path), **read_metrics(path),
    }


@contextmanager
def sample_gpu(enabled, folder):
    executable = shutil.which("nvidia-smi") if enabled else None
    if not executable:
        if enabled:
            raise ValueError("nvidia-smi is required for --gpu-telemetry")
        yield
        return
    query = "timestamp,index,pstate,clocks.gr,clocks.mem,utilization.gpu,power.draw,temperature.gpu"
    with (folder / "gpu.csv").open("wb") as stream:
        process = subprocess.Popen(
            [executable, f"--query-gpu={query}", "--format=csv,nounits", "-l", "1"],
            stdout=stream, creationflags=subprocess.CREATE_NO_WINDOW,
        )
        try:
            yield
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait()


def run_match(args, engine, run_dir, map_id, quality, source, level, url):
    folder = run_dir / f"{map_id}-{PRESETS[quality]}"
    folder.mkdir()
    log = folder / "game.log"
    profile = folder / "profile"
    profile.mkdir()
    width, height = args.resolution
    env = dict(os.environ, SCCT_BENCHMARK=json.dumps({
        "world": level + "." + level.rsplit("/", 1)[1], "compare_cluster_images": args.compare_cluster_images, "motion": args.aa_motion,
        "log": str(log), "quality": quality, "route_steps": len(ROUTE), "clustered": args.clustered_lights, "aa": args.aa,
    }))
    route_script = Path(__file__).with_name("measure_route.py").resolve()
    args_line = " ".join((
        subprocess.list2cmdline([str(engine), str(args.project / "OpenSCCTUnreal.uproject")]),
        f"{url}?Mode=Story?Side=merc?Bots={args.bots}?Practice=1?PlayLevel=1",
        "-game -d3d12 -windowed -ForceRes -unattended -nosplash -Multiprocess",
        "-ini:Engine:[ConsoleVariables]:EnhancedInput.bEnableAutoUpgrade=0",
        f"-ResX={width} -ResY={height}",
        "-ScctNoAccountSync -ScctNoTokenStore -ScctNoPresenceLease",
        switch("ScctAutoplay", ";".join(ROUTE + (MOTION_TAIL if args.aa_motion else ()))),
        switch("ScctContent", args.content.as_posix()) if args.content else "",
        switch("UserDir", profile.as_posix()),
        switch("abslog", log.as_posix()),
        switch("ExecCmds", f"py {route_script.as_posix()}"),
    ))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    with sample_gpu(args.gpu_telemetry, folder), subprocess.Popen(args_line, cwd=args.project, env=env, startupinfo=startup) as process:
        try:
            exit_code = process.wait(timeout=args.timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            process.kill()
            process.wait()
            raise
    if exit_code:
        raise ValueError(f"Unreal exited with code {exit_code}")
    capture = validate_capture(log, profile, map_id, source, args.resolution, quality, args.bots, args.aa_motion)
    if args.clustered_lights and capture["settings"]["clustered_lights"] != 1:
        raise ValueError("clustered deferred lighting did not enable")
    expected_aa = {"FXAA": 1, "TAA": 2, "TSR": 4}
    if args.aa in expected_aa and capture["settings"]["anti_aliasing_method"] != expected_aa[args.aa]:
        raise ValueError("requested anti-aliasing method did not activate")
    images = []
    if args.aa_motion:
        images = [folder / f"pan_{index}.png" for index in range(4)]
    elif args.compare_cluster_images:
        images = [folder / (name + ".png") for name in ("unclustered", "clustered", "unclustered_repeat")]
    if any(not image.is_file() or image.stat().st_size == 0 for image in images):
        raise ValueError("a requested post-route image was not saved")
    if images:
        capture["images"] = [str(image) for image in images]
    if args.gpu_telemetry:
        capture["gpu_telemetry"] = str(folder / "gpu.csv")
    return capture


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--resolution", type=parse_resolution, required=True, help="native viewport, e.g. 2560x1440")
    parser.add_argument("--maps", nargs="+", default=["MounE", "AquaE", "ClubE", "PolaE"], metavar="MAP")
    parser.add_argument("--presets", nargs="+", choices=PRESETS, default=list(PRESETS))
    pictures = parser.add_mutually_exclusive_group()
    pictures.add_argument("--aa-motion", action="store_true", help="save frames during a controlled camera pan after timing")
    pictures.add_argument("--compare-cluster-images", action="store_true", help="capture an unclustered, clustered and repeated unclustered view after timing")
    parser.add_argument("--gpu-telemetry", action="store_true", help="record NVIDIA clock, utilization and power once per second")
    parser.add_argument("--aa", choices=("Auto", "FXAA", "TAA", "TSR"), default="Auto")
    parser.add_argument("--clustered-lights", action="store_true", help="probe clustered deferred lighting on this route")
    parser.add_argument("--map-source", choices=("auto", "cooked", "runtime"), default="auto")
    parser.add_argument("--bots", type=int, choices=range(4), default=2)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--engine", type=Path, default=Path("F:/UE_5.8"), help="Unreal Engine root")
    parser.add_argument("--content", type=Path, default=os.getenv("SVM_CONTENT") or None,
                        help="converted content directory (defaults to SVM_CONTENT or game discovery)")
    parser.add_argument("--output", type=Path, help="parent directory for a fresh run directory")
    parser.add_argument("--timeout", type=int, default=180, help="maximum seconds per standalone match")
    args = parser.parse_args()
    args.project = args.project.resolve()
    if args.content:
        args.content = args.content.resolve()
    engine = args.engine.resolve() / "Engine/Binaries/Win64/UnrealEditor.exe"
    if args.timeout <= 0 or not engine.is_file() or not (args.project / "OpenSCCTUnreal.uproject").is_file():
        parser.error("positive timeout, UnrealEditor.exe and OpenSCCTUnreal.uproject required")
    if len({item.lower() for item in args.maps}) != len(args.maps) or len(set(args.presets)) != len(args.presets):
        parser.error("map and preset lists must be unique")
    if not all(re.fullmatch(r"[A-Za-z0-9_-]+", item) for item in args.maps):
        parser.error("use map IDs such as MounE or MoonD")
    try:
        maps = {map_id: select_level(args, map_id) for map_id in args.maps}
    except ValueError as error:
        parser.error(str(error))

    output = (args.output or args.project / "Saved/PerformanceQA").resolve()
    output.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="benchmark-", dir=output))
    report = {"project": str(args.project), "viewport": args.resolution, "bots": args.bots, "renderer": "Unreal", "clustered_lights": args.clustered_lights, "aa": args.aa, "runs": []}
    print(f"Benchmark output: {run_dir}", flush=True)
    for map_id, location in maps.items():
        for preset in args.presets:
            print(f"Running {map_id} {preset} ({location[0]})", flush=True)
            try:
                result = run_match(args, engine, run_dir, map_id, PRESETS.index(preset), *location)
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                parser.exit(1, f"{map_id} {preset}: {error}. See {run_dir / (map_id + '-' + preset)}\n")
            report["runs"].append(result)
            (run_dir / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(f"  median {result['median_ms']:.3f} ms, p99 {result['p99_ms']:.3f} ms, "
                  f"{result['within_4_167_ms_percent']:.2f}% at 240 FPS or faster", flush=True)


if __name__ == "__main__":
    main()
