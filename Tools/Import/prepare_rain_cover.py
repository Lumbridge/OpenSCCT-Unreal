"""Works out what stands between the sky and the ground inside each rain volume of a converted map.

    python prepare_rain_cover.py <content root>/svmap/<Map> <output folder>

Rain falls from the top of its volume and stops at the first thing it meets: a roof, an awning, a tree, the
ground. For every rain volume this writes the height of that first thing, seen from straight above, as a
picture: <output>/RainCover_<volume>.png (16-bit grey, row 0 at the volume's low Y, column 0 at its low X) and
its place in <output>/rain_cover.json ({volume: {"origin": [x, y, z], "size": [x, y, z], "file": ...}}; a
pixel's height is origin z + value * size z). The importer hands both to the rain's material and to the wet
ground under it (scct_fx.py, place_rain), so no drop is drawn below a roof and no roofed floor is wet.

The heights come from the map's render geometry (world.glb): the level's own surfaces and every placed mesh,
in the place the map gives it. Runs in the system's Python (numpy and Pillow), before the editor import.
"""
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

# The picture's cell is about this many world units, and a volume never gets more cells than this a side.
CELL = 16.0
MOST_CELLS = 512
# How far under a rain volume's floor the cover still counts (scct_fx.WET_BELOW_VOLUME reaches as far).
BELOW = 64.0

COMPONENTS = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read_glb(path):
    with open(path, "rb") as file:
        file.read(12)
        length, _ = struct.unpack("<I4s", file.read(8))
        document = json.loads(file.read(length))
        length, _ = struct.unpack("<I4s", file.read(8))
        return document, file.read(length)


def accessor(document, binary, index):
    record = document["accessors"][index]
    view = document["bufferViews"][record["bufferView"]]
    kind = np.dtype(COMPONENTS[record["componentType"]])
    width = WIDTHS[record["type"]]
    start = view.get("byteOffset", 0) + record.get("byteOffset", 0)
    stride = view.get("byteStride") or kind.itemsize * width
    if stride == kind.itemsize * width:
        data = np.frombuffer(binary, kind, record["count"] * width, start)
    else:
        raw = np.frombuffer(binary, np.uint8, record["count"] * stride, start).reshape(record["count"], stride)
        data = raw[:, :kind.itemsize * width].copy().view(kind)
    return data.reshape(record["count"], width) if width > 1 else data


def mesh_triangles(document, binary, mesh_index):
    """The triangles of a mesh as an (n, 3, 3) array in the game's coordinates (glTF is metres with Y up; the
    engine's importer reads it as x, z, y in centimetres)."""
    parts = []
    for primitive in document["meshes"][mesh_index]["primitives"]:
        if primitive.get("mode", 4) != 4 or "POSITION" not in primitive["attributes"]:
            continue
        positions = accessor(document, binary, primitive["attributes"]["POSITION"]).astype(np.float64)
        positions = positions[:, [0, 2, 1]] * 100.0
        if "indices" in primitive:
            indices = accessor(document, binary, primitive["indices"]).astype(np.int64)
        else:
            indices = np.arange(len(positions))
        parts.append(positions[indices[:len(indices) // 3 * 3]].reshape(-1, 3, 3))
    return np.concatenate(parts) if parts else np.zeros((0, 3, 3))


def rotation_axes(rotation):
    """The engine's rotation matrix for the original's 65536-per-turn Pitch, Yaw, Roll: the rows are where the
    local x, y and z axes point."""
    pitch, yaw, roll = (rotation.get(name, 0) * 2.0 * math.pi / 65536.0 for name in ("Pitch", "Yaw", "Roll"))
    cp, sp, cy, sy, cr, sr = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw), math.cos(roll), math.sin(roll)
    return np.array([[cp * cy, cp * sy, sp],
                     [sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp],
                     [-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp]])


def world_triangles(document, binary):
    """Every triangle of the level as drawn: the level's own surfaces and the placed meshes (not the sky zone's,
    not hidden ones, not the copies that only carry a projected picture)."""
    node_mesh = {node["name"]: node["mesh"] for node in document["nodes"] if "mesh" in node and "name" in node}
    parts = []
    for node in document["nodes"]:
        role = node.get("extras", {}).get("svmap", {})
        if role.get("role") == "bsp" and not role.get("sky") and "mesh" in node:
            parts.append(mesh_triangles(document, binary, node["mesh"]))
    cache = {}
    for instance in document["extras"]["svmap"]["instances"]:
        if instance.get("hidden") or instance.get("skyZone") or instance.get("projectorOf"):
            continue
        mesh = node_mesh.get(instance["mesh"])
        if mesh is None:
            continue
        if mesh not in cache:
            cache[mesh] = mesh_triangles(document, binary, mesh)
        local = cache[mesh]
        if not len(local):
            continue
        scale = np.array(instance.get("scale", [1.0, 1.0, 1.0]), dtype=np.float64)
        pivot = np.array(instance.get("prePivot") or [0.0, 0.0, 0.0], dtype=np.float64)
        # world = ((local - PrePivot) * Scale) rotated + Location
        parts.append(((local - pivot) * scale) @ rotation_axes(instance["rotation"]) + np.array(instance["location"], dtype=np.float64))
    return np.concatenate(parts) if parts else np.zeros((0, 3, 3))


def cover_of(triangles, low, high, columns, rows):
    """The highest surface under `high[2]` in each cell of the box, as heights (low[2] where there is none)."""
    cover = np.full((rows, columns), low[2], dtype=np.float64)
    least, most = triangles.min(axis=1), triangles.max(axis=1)
    inside = ((most[:, 0] >= low[0]) & (least[:, 0] <= high[0]) & (most[:, 1] >= low[1]) & (least[:, 1] <= high[1])
              & (most[:, 2] > low[2]) & (least[:, 2] < high[2]))
    step_x, step_y = (high[0] - low[0]) / columns, (high[1] - low[1]) / rows
    for a, b, c in triangles[inside]:
        # Seen from above: a wall is a line and covers nothing.
        area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(area) < 1.0:
            continue
        x0 = max(int(math.floor((min(a[0], b[0], c[0]) - low[0]) / step_x)), 0)
        x1 = min(int(math.floor((max(a[0], b[0], c[0]) - low[0]) / step_x)), columns - 1)
        y0 = max(int(math.floor((min(a[1], b[1], c[1]) - low[1]) / step_y)), 0)
        y1 = min(int(math.floor((max(a[1], b[1], c[1]) - low[1]) / step_y)), rows - 1)
        if x1 < x0 or y1 < y0:
            continue
        x = low[0] + (np.arange(x0, x1 + 1) + 0.5) * step_x
        y = low[1] + (np.arange(y0, y1 + 1) + 0.5) * step_y
        px, py = np.meshgrid(x, y)
        u = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area
        v = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area
        w = 1.0 - u - v
        # A little past the edges, so two triangles that share an edge leave no uncovered cell between them.
        hit = (u >= -0.02) & (v >= -0.02) & (w >= -0.02)
        if not hit.any():
            continue
        height = np.clip(u * a[2] + v * b[2] + w * c[2], low[2], high[2])
        # What stands above the volume's top is not in the rain's way: the rain starts below it.
        height = np.where(u * a[2] + v * b[2] + w * c[2] >= high[2], low[2], height)
        patch = cover[y0:y1 + 1, x0:x1 + 1]
        np.maximum(patch, np.where(hit, height, low[2]), out=patch)
    return cover


def main():
    map_folder, output = sys.argv[1], sys.argv[2]
    os.makedirs(output, exist_ok=True)
    rain_path = os.path.join(map_folder, "sim", "rain.json")
    volumes = json.load(open(rain_path)).get("volumes", []) if os.path.exists(rain_path) else []
    places = {}
    if volumes:
        document, binary = read_glb(os.path.join(map_folder, "render", "world.glb"))
        triangles = world_triangles(document, binary)
        for volume in volumes:
            scale = volume.get("scale", [1.0, 1.0, 1.0])
            half_x, half_y, height = (volume["size"][axis] * abs(scale[axis]) for axis in range(3))
            if min(half_x, half_y, height) <= 0.0:
                continue
            x, y, z = volume["location"]
            # The volume's box is (-X, -Y, 0) .. (X, Y, Z) from its location.
            low = (x - half_x, y - half_y, z - BELOW)
            high = (x + half_x, y + half_y, z + height)
            columns = int(min(MOST_CELLS, max(8, round(2.0 * half_x / CELL))))
            rows = int(min(MOST_CELLS, max(8, round(2.0 * half_y / CELL))))
            cover = cover_of(triangles, low, high, columns, rows)
            values = np.round((cover - low[2]) / (high[2] - low[2]) * 65535.0).astype(np.uint16)
            file = "RainCover_%s.png" % volume["name"]
            Image.fromarray(values).save(os.path.join(output, file))
            places[volume["name"]] = {"origin": list(low), "size": [high[axis] - low[axis] for axis in range(3)], "file": file}
            roofed = float((cover > low[2] + 0.6 * (high[2] - low[2])).mean())
            print("rain cover %s: %d x %d cells, %.0f%% of it under something in the upper part of the volume" % (
                volume["name"], columns, rows, roofed * 100.0))
    json.dump(places, open(os.path.join(output, "rain_cover.json"), "w"), indent=1)


main()
