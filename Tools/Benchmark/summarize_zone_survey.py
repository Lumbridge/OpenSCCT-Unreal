"""Read a zone sweep from Unreal's finalized CSV and its per-zone camera timings."""
import argparse
import csv
import json
import math
from pathlib import Path
from statistics import mean, median


def summarize(folder, name):
    zones = json.loads((folder / f"{name}.json").read_text())
    csv_path = folder / "profile" / "Saved" / "Profiling" / "CSV" / f"{name}.csv"
    with csv_path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        times = ("FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime",
                 "Exclusive/RenderThread/InitViews_Scene", "ScctSession/GameThread/Simulate")
        fields = [k for k in reader.fieldnames if k in times or k.startswith("GPU/")]
        series = {key: [] for key in fields}
        for row in reader:
            if row["FrameTime"] == "FrameTime":
                break
            for key, values in series.items():
                values.append(float(row[key] or 0))
    frames = sorted(series["FrameTime"])
    seconds = sum(frames) / 1000
    sweep_time = sum(z["seconds"] for z in zones)
    if not zones or not frames or abs(sweep_time - seconds) > max(2, 0.05*sweep_time):
        raise ValueError(f"Camera sweep and frame capture durations disagree for {name}")
    pass_means = {k[4:]: round(mean(v), 3) for k, v in series.items() if k.startswith("GPU/")}
    return {
        "zones": len(zones), "frames": len(frames), "seconds": round(seconds, 2),
        "fps": round(len(frames)/seconds, 2), "p95_frame_ms": round(frames[math.ceil(len(frames)*.95)-1], 3),
        "percent_within_240_fps_budget": round(100*sum(t<=1000/240 for t in frames)/len(frames), 3),
        "median_ms": {k: round(median(series[k]), 3) for k in times if k in series},
        "gpu_pass_mean_ms": dict(sorted(pass_means.items(), key=lambda item: -item[1])),
        "per_zone_fps": [{**zone, "fps": round(zone["frames"]/zone["seconds"], 1)} for zone in zones],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("names", nargs="+")
    args = parser.parse_args()
    report = {name: summarize(args.folder, name) for name in args.names}
    (args.folder/"timings.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    for name, run in report.items():
        print(f"{name}: {run['zones']} zones, {run['fps']} fps, frame p95 {run['p95_frame_ms']} ms")
