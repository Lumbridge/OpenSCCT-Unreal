"""Checks the lighting plan's reading of the map lights' flags against the lightmaps themselves.

    python Tools/Light/check_baked_flags.py <svmap folder> [Map ...] [--csv out.csv] [--all] [--control]

(<svmap folder> holds <Map>/render/lighting.json, world.glb and the lightmap pages; without map names every map in it.
Needs numpy and Pillow. Reads only.)

The plan (Source/OpenSCCT/Play/ScctLightRules.h) keeps a light LIVE on the world when it is dynamic or a projector
(LightEffect 22), whatever its bApplyToStaticLighting says, because the original kept such lights out of its bake; and
it shows the lightmaps at full strength. If one of those lights were in the lightmap after all, its pool would be
drawn twice. So for every live light that carries the static flag this looks at the lightmapped surfaces round it:

  inside   the samples the light's own term reaches at 0.5 or more (facing it, in its cone or frustum)
  ring     the samples just outside its reach (1.05 to 1.6 of it), or inside it where its term is under 0.1
  rank     the rank correlation, over the samples within 1.3 of its reach, of its term with the baked level

A light that is in the lightmap shows a pool: inside well above ring, and the baked level following its term. A baked
lamp standing at the same fixture (within half the reach) would show the same, so those are told apart and not
counted. No shadow is traced (a wall between counts as lit by the term), which blurs the measure towards "not baked".

  DOUBLE   inside >= 2 x ring + 0.1, rank >= 0.4, at least 20 samples inside, no baked lamp beside it
  beside   the same picture, but a baked lamp stands within half the reach: that lamp explains it
  -        no pool of its own in the lightmap

Levels are the original's term: 2 x the stored value, 1 = a texture at its full brightness.

--control asks the same of the lamps the plan takes as baked (static flag, not dynamic, no projector), where a pool is
expected: how many of them the measure finds says how much a "none found" above is worth. (It finds a minority: a
baked lamp's pool is cut by walls this does not trace, and lamps stand close together.)
"""
import argparse
import csv
import json
import math
import os
import struct

import numpy as np
from PIL import Image

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
LUM = np.array([0.3, 0.59, 0.11])


def load_glb(path):
    with open(path, "rb") as f:
        f.read(12)
        length, _ = struct.unpack("<II", f.read(8))
        gltf = json.loads(f.read(length))
        length, _ = struct.unpack("<II", f.read(8))
        return gltf, f.read(length)


def accessor(gltf, data, index):
    a = gltf["accessors"][index]
    view = gltf["bufferViews"][a["bufferView"]]
    kind, n = CT[a["componentType"]], NC[a["type"]]
    offset = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = view.get("byteStride", 0)
    size = np.dtype(kind).itemsize * n
    if stride and stride != size:
        raw = np.frombuffer(data, np.uint8, stride * a["count"], offset).reshape(a["count"], stride)[:, :size].copy().view(kind)
        out = raw.reshape(a["count"], n)
    else:
        out = np.frombuffer(data, kind, a["count"] * n, offset).reshape(a["count"], n)
    if a.get("normalized") and kind != np.float32:
        out = out.astype(np.float32) / np.iinfo(kind).max
    return out


def to_unreal(v):
    """glTF metres, Y up, to the map's units."""
    return np.stack([v[:, 0], v[:, 2], v[:, 1]], 1).astype(np.float64) * 100.0


def axes(pitch, yaw, roll):
    """The rows are the forward, right and up axes of a rotation in the original's units (65536 to a turn)."""
    k = math.pi / 32768
    p, y, r = pitch * k, yaw * k, roll * k
    cp, sp, cy, sy, cr, sr = math.cos(p), math.sin(p), math.cos(y), math.sin(y), math.cos(r), math.sin(r)
    return np.array([[cp * cy, cp * sy, sp], [sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp], [-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp]])


def samples(folder, rng):
    """Area-weighted points on the lightmapped world surfaces: place, normal, baked level."""
    lighting = json.load(open(os.path.join(folder, "render", "lighting.json")))
    gltf, data = load_glb(os.path.join(folder, "render", "world.glb"))
    pages = {}
    for page in lighting.get("lightmaps", []):
        pages[page] = np.asarray(Image.open(os.path.join(folder, "render", page)).convert("RGB"), np.float32) / 255.0
    page_of = {i: (m.get("extras") or {}).get("scct_lightmap") for i, m in enumerate(gltf["materials"])}
    places, normals, levels = [], [], []
    for node in gltf["nodes"]:
        info = node.get("extras", {}).get("svmap", {})
        if info.get("role") != "bsp" or info.get("sky"):
            continue
        for part in gltf["meshes"][node["mesh"]]["primitives"]:
            page = page_of.get(part.get("material")) if "TEXCOORD_1" in part["attributes"] else None
            if page is None or page not in pages:
                continue
            p = to_unreal(accessor(gltf, data, part["attributes"]["POSITION"]))
            n = to_unreal(accessor(gltf, data, part["attributes"]["NORMAL"])) / 100.0
            uv = accessor(gltf, data, part["attributes"]["TEXCOORD_1"]).astype(np.float64)
            index = accessor(gltf, data, part["indices"]).reshape(-1, 3).astype(np.int64)
            a, b, c = p[index[:, 0]], p[index[:, 1]], p[index[:, 2]]
            area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
            count = np.clip(np.ceil(area / (40.0 ** 2)), 1, 600).astype(int)
            tri = np.repeat(np.arange(len(area)), count)
            r1, r2 = np.sqrt(rng.random(len(tri))), rng.random(len(tri))
            w0, w1, w2 = 1 - r1, r1 * (1 - r2), r1 * r2
            at = a[tri] * w0[:, None] + b[tri] * w1[:, None] + c[tri] * w2[:, None]
            st = uv[index[:, 0]][tri] * w0[:, None] + uv[index[:, 1]][tri] * w1[:, None] + uv[index[:, 2]][tri] * w2[:, None]
            image = pages[page]
            height, width = image.shape[:2]
            x = np.clip((st[:, 0] * width).astype(int), 0, width - 1)
            y = np.clip((st[:, 1] * height).astype(int), 0, height - 1)
            places.append(at)
            normals.append(n[index[:, 0]][tri])
            levels.append(2.0 * (image[y, x].astype(np.float64) @ LUM))
    if not places:
        return lighting, np.zeros((0, 3)), np.zeros((0, 3)), np.zeros(0)
    return lighting, np.concatenate(places), np.concatenate(normals), np.concatenate(levels)


def peak_of(light):
    """The light's peak term in display units, as the bridge's light table has it (FGetHSV x brightness / 255 x 2)."""
    colour = light.get("color") or [1.0, 1.0, 1.0]
    most = max(colour[:3]) if colour else 1.0
    if light["effect"] == 22:
        return 2.0 * min(1.0, most * light["brightness"] / 510.0)
    return 2.0 * most * light["brightness"] / 255.0


def term(light, places, normals):
    """The light's own term at the samples (no shadow), and their distance as a share of its reach."""
    at = np.array(light["location"], float)
    to_light = at[None] - places
    distance = np.linalg.norm(to_light, axis=1)
    facing = np.clip((to_light * normals).sum(1) / np.maximum(distance, 1e-6), 0.0, None)
    rotation = light.get("rotation") or [0, 0, 0]
    forward, right, up = axes(rotation[0], rotation[1], rotation[2])
    peak = peak_of(light)
    if light["effect"] == 22:
        reach = max(float(light.get("echelon_range") or 1000.0), 1.0)
        ratio = float(light.get("near_plane_ratio") or 0.1)
        apex = ratio * reach / (1.0 - ratio) if ratio < 0.999 else 1.0e9
        depth = (-to_light * forward).sum(1)
        wide = np.abs((-to_light * right).sum(1))
        high = np.abs((-to_light * up).sum(1))
        grow = (apex + depth) / (apex + reach)
        inside = (depth > 0) & (depth < reach) & (wide <= float(light.get("spot_width") or 250.0) * grow) & (high <= float(light.get("spot_height") or 250.0) * grow)
        return np.where(inside, peak * (1.0 - depth / reach), 0.0), np.where(depth > 0, depth, distance) / reach, reach
    reach = 25.0 * (light["radius"] + 1.0)
    x = np.clip(distance / reach, 0.0, 1.0)
    value = peak * (1.0 - 3.0 * x * x + 2.0 * x ** 3) * facing
    if light.get("light_type") == "spot":
        half = 0.5 * 1.15 * math.acos((1.0 - min(light["cone"], 255) / 256.0) ** 2)
        cosine = (-to_light * forward).sum(1) / np.maximum(distance, 1e-6)
        inner = math.cos(0.05 * half)
        outer = math.cos(max(half, 0.01))
        value = value * np.clip((cosine - outer) / max(inner - outer, 1e-6), 0.0, 1.0)
    return value, distance / reach, reach


def rank_correlation(a, b):
    if len(a) < 8 or a.std() == 0 or b.std() == 0:
        return float("nan")
    ra, rb = np.argsort(np.argsort(a)).astype(float), np.argsort(np.argsort(b)).astype(float)
    return float(np.corrcoef(ra, rb)[0, 1])


def check(root, name, rng, show_all, control=False):
    folder = os.path.join(root, name)
    lighting, places, normals, levels = samples(folder, rng)
    lights = [light for light in lighting["lights"] if not light.get("hidden")]
    baked = [light for light in lights if light["apply_static"] and not light["dynamic"] and light["effect"] != 22 and not light.get("special_lit")]
    baked_at = np.array([light["location"] for light in baked], float) if baked else np.zeros((0, 3))
    live = [light for light in lights if (light["dynamic"] or light["effect"] == 22) and not light.get("special_lit")]
    rows = []
    for light in (baked if control else live):
        if not light["apply_static"] or len(places) == 0:
            continue
        value, share, reach = term(light, places, normals)
        inside = value >= 0.5
        ring = ((share > 1.05) & (share < 1.6)) | ((share <= 1.0) & (value < 0.1))
        near = share < 1.3
        beside = float(np.linalg.norm(baked_at - np.array(light["location"], float)[None], axis=1).min() / reach) if len(baked_at) and not control else float("inf")
        mean_in = float(levels[inside].mean()) if inside.any() else float("nan")
        mean_ring = float(levels[ring].mean()) if ring.any() else float("nan")
        rank = rank_correlation(value[near], levels[near]) if near.any() else float("nan")
        pool = int(inside.sum()) >= 20 and mean_in >= 2.0 * mean_ring + 0.1 and rank >= 0.4
        verdict = ("beside" if beside <= 0.5 else "DOUBLE") if pool else "-"
        rows.append({"map": name, "light": light["name"], "class": "projector" if light["effect"] == 22 else "dynamic", "triggered": int(bool(light.get("triggered"))),
                     "on": int(bool(light.get("initially_on", True)) and light.get("ltype", 1) != 0), "peak": round(peak_of(light), 3), "reach": round(reach),
                     "inside_n": int(inside.sum()), "inside": round(mean_in, 3), "ring": round(mean_ring, 3), "rank": round(rank, 3),
                     "baked_lamp_at": round(beside, 2) if math.isfinite(beside) else "", "verdict": verdict})
    double = [r for r in rows if r["verdict"] == "DOUBLE"]
    beside = [r for r in rows if r["verdict"] == "beside"]
    if control:
        judged = [r for r in rows if r["inside_n"] >= 20]
        print("%-8s control: %3d baked lamps, %3d with 20 samples or more inside; a pool of their own shows for %d" % (name, len(rows), len(judged), len(double)))
        return rows
    print("%-8s %4d lights: %3d live (dynamic or projector), %3d of them with the static flag; %d show a pool of their own in the lightmap (DOUBLE), %d a pool a baked lamp beside them explains; %d lightmap samples" % (
        name, len(lights), len(live), len(rows), len(double), len(beside), len(places)))
    for r in rows:
        if show_all or r["verdict"] != "-":
            print("     %-6s %-22s %-9s %s peak %.2f reach %5d: inside %.2f (n=%d), ring %.2f, rank %+.2f, nearest baked lamp at %s of the reach" % (
                r["verdict"], r["light"], r["class"], "on " if r["on"] else "off", r["peak"], r["reach"], r["inside"], r["inside_n"], r["ring"], r["rank"], r["baked_lamp_at"]))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("root")
    parser.add_argument("maps", nargs="*")
    parser.add_argument("--csv")
    parser.add_argument("--all", action="store_true", help="print every checked light, not only those with a pool")
    parser.add_argument("--control", action="store_true", help="ask the same of the baked lamps, where a pool is expected")
    args = parser.parse_args()
    maps = args.maps or sorted(m for m in os.listdir(args.root) if os.path.exists(os.path.join(args.root, m, "render", "lighting.json"))
                               and os.path.exists(os.path.join(args.root, m, "render", "world.glb")))
    rng = np.random.default_rng(7)
    rows, failed = [], []
    for name in maps:
        try:
            rows += check(args.root, name, rng, args.all, args.control)
        except Exception as error:  # pylint: disable=broad-except
            failed.append(name)
            print("%-8s could not be read: %s: %s" % (name, type(error).__name__, error))
    double = [r for r in rows if r["verdict"] == "DOUBLE"]
    # All the lights together say more than any one of them: a set of lights that is in the lightmaps has its
    # inside above its ring on the whole, a set that is not has the two alike.
    both = [r for r in rows if r["inside_n"] >= 20 and r["ring"] == r["ring"]]
    if both:
        inside, ring = [r["inside"] for r in both], [r["ring"] for r in both]
        print("over the %d lights with 20 samples or more inside: baked level inside %.3f on average (median %.3f), in the ring %.3f (median %.3f); inside is over 1.5 x the ring for %.0f %%, under the ring for %.0f %%" % (
            len(both), sum(inside) / len(both), sorted(inside)[len(both) // 2], sum(ring) / len(both), sorted(ring)[len(both) // 2],
            100.0 * sum(1 for a, b in zip(inside, ring) if a > 1.5 * b) / len(both), 100.0 * sum(1 for a, b in zip(inside, ring) if a < b) / len(both)))
    if args.control:
        judged = [r for r in rows if r["inside_n"] >= 20]
        print("%d maps (%d could not be read), control: %d baked lamps, %d with 20 samples or more inside, a pool of their own shows for %d (%.0f %%)" % (
            len(maps), len(failed), len(rows), len(judged), len(double), 100.0 * len(double) / max(len(judged), 1)))
    else:
        print("%d maps (%d could not be read), %d live lights with the static flag: %d DOUBLE on %d maps, %d beside a baked lamp" % (
            len(maps), len(failed), len(rows), len(double), len(set(r["map"] for r in double)), sum(1 for r in rows if r["verdict"] == "beside")))
    if args.csv and rows:
        with open(args.csv, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
