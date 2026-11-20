"""Stage the authored Side Stairs skylight slices and their grate projection."""

import argparse
import json
import math
from pathlib import Path
import struct

import numpy as np
from PIL import Image

from prepare_hospital_lightmaps import accessor, read_glb


LIGHT = "Light3517"
GRATE = "StaticMeshActor1172"
MASK = "Prison_TEX_metalgrille.png"
TRANSMISSION = "T_MounE_SideStairsTransmission.png"
CAST_DEPTH = "T_MounE_SideStairsCastDepth.png"
ASSET = "SM_MounE_SideStairsShaft"


def axes(rotation):
    p, y, r = (angle * math.tau / 65536.0 for angle in rotation)
    sp, sy, sr = map(math.sin, (p, y, r))
    cp, cy, cr = map(math.cos, (p, y, r))
    return np.array(((cp * cy, cp * sy, sp),
                     (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp),
                     (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)))


def placement_frame(placement):
    rotation = placement["rotation"]
    return axes([rotation["Pitch"], rotation["Yaw"], rotation["Roll"]])


def grate_frame(document, binary):
    placement = next(entry for entry in document["extras"]["svmap"]["instances"] if entry["actor"] == GRATE)
    node = next(entry for entry in document["nodes"] if entry.get("name") == placement["mesh"])
    mesh = document["meshes"][node["mesh"]]
    grate = next(p for p in mesh["primitives"] if document["materials"][p["material"]]["name"] == "Prison_TEX.metalgrille")
    attrs = grate["attributes"]
    pos = accessor(document, binary, attrs["POSITION"], "VEC3", {5126: "<f4"})[:, [0, 2, 1]] * 100.0
    uv = accessor(document, binary, attrs["TEXCOORD_0"], "VEC2", {5126: "<f4"})
    world = (pos * placement["scale"]) @ placement_frame(placement) + placement["location"]
    if np.ptp(world[:, 2]) > 0.05:
        raise ValueError("Expected a planar skylight grate")
    grate_centre = world.mean(axis=0)
    points = np.column_stack((world[:, :2] - grate_centre[:2], np.ones(len(world))))
    to_uv, _, rank, _ = np.linalg.lstsq(points, uv, rcond=None)
    if rank != 3 or np.max(np.abs(points @ to_uv - uv)) > 0.002:
        raise ValueError("Could not resolve the skylight grate's UV frame")
    return grate_centre, to_uv.T


def shadow_triangles(document, binary, render, lower, upper):
    # Use the rendered BSP and fixed opaque meshes. The grate has its own alpha mask and the authored glass
    # explicitly ignores this light. Avoid baking movable/gameplay actors into a fixed aperture.
    flags = json.loads((render / "materials.json").read_text(encoding="utf-8"))["materials"]
    meshes = {node["name"]: document["meshes"][node["mesh"]] for node in document["nodes"] if "mesh" in node}

    def opaque_triangles(mesh):
        triangles = []
        for primitive in mesh["primitives"]:
            material = document["materials"][primitive["material"]]
            authored = flags.get(material["name"], {}).get("flags", {})
            if (material.get("alphaMode", "OPAQUE") != "OPAQUE" or authored.get("unlit")
                    or authored.get("masked") or authored.get("translucent")):
                continue
            attr = primitive["attributes"]["POSITION"]
            vertices = accessor(document, binary, attr, "VEC3", {5126: "<f4"})[:, [0, 2, 1]] * 100.0
            indices = accessor(document, binary, primitive["indices"], "SCALAR", {5123: "<u2", 5125: "<u4"}).reshape(-1, 3)
            triangles.append(vertices[indices])
        return np.concatenate(triangles) if triangles else np.empty((0, 3, 3))

    def within_beam(triangles):
        intersects = ((triangles.max(axis=1) >= lower) & (triangles.min(axis=1) <= upper)).all(axis=1)
        return triangles[intersects]

    regions = [within_beam(opaque_triangles(meshes["BSP_MounE"]))]
    for placed in document["extras"]["svmap"]["instances"]:
        if (placed["actor"] == GRATE or placed.get("gameplay") or placed.get("hidden") or placed.get("projectorOf")
                or placed.get("kind") != "staticmesh" or LIGHT in placed.get("ignoredEchelonLights", [])):
            continue
        location = np.asarray(placed["location"])
        if np.any(location < lower - 850) or np.any(location > upper + 850):
            continue
        mesh = meshes.get(placed["mesh"])
        if mesh is not None:
            triangles = opaque_triangles(mesh)
            world = (triangles * placed["scale"]) @ placement_frame(placed) + location
            regions.append(within_beam(world))
    return np.concatenate(regions)


def first_cast_depth(triangles, apex, grate, uv, maximum, size=256):
    yy, xx = np.mgrid[:size, :size]
    uv_centres = (np.column_stack((xx.ravel(), yy.ravel())) + 0.5) / size
    grate_xy = (uv_centres - uv[:, 2]) @ np.linalg.inv(uv[:, :2]).T + grate[:2]
    starts = np.column_stack((grate_xy, np.full(len(grate_xy), grate[2])))
    near = apex[2] - grate[2]
    rays = (starts - apex) / near  # dz = -1: ray parameter is vertical distance downward from the apex.

    a, b, c = np.moveaxis(triangles, 1, 0)
    e1, e2 = b - a, c - a
    start = apex - a
    q = np.cross(start, e1)
    tnum = np.einsum("ti,ti->t", e2, q)
    depths = np.empty(len(rays), dtype=np.float32)
    for i in range(0, len(rays), 256):
        direction = rays[i:i + 256]
        h = np.cross(direction[:, None], e2[None])
        det = np.einsum("rti,ti->rt", h, e1)
        inv = np.divide(1.0, det, out=np.zeros_like(det), where=np.abs(det) > 1e-8)
        u = np.einsum("rti,ti->rt", h, start) * inv
        v = (direction @ q.T) * inv
        t = tnum * inv - near
        hits = (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1.000001) & (t > 0.5)
        depths[i:i + len(direction)] = np.minimum(np.min(np.where(hits, t, np.inf), axis=1), maximum)
    return depths.reshape(size, size)


def write_mesh(path, positions, triangles):
    # Converted-map glTF positions are metres with Y up. Reflect the triangle winding with the axis swap.
    pos = np.asarray(positions[:, [0, 2, 1]] / 100.0, dtype="<f4")
    indices = np.asarray(triangles[:, [0, 2, 1]], dtype="<u2")
    content = pos.tobytes() + indices.tobytes()
    content += b"\0" * (-len(content) % 4)
    doc = {
        "asset": {"version": "2.0"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": ASSET, "mesh": 0}],
        "meshes": [{"name": ASSET, "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4}]}],
        "buffers": [{"byteLength": len(content)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": pos.nbytes, "target": 34962},
                        {"buffer": 0, "byteOffset": pos.nbytes, "byteLength": indices.nbytes, "target": 34963}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3",
                       "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()},
                      {"bufferView": 1, "componentType": 5123, "count": indices.size, "type": "SCALAR"}],
    }
    document = json.dumps(doc, separators=(",", ":")).encode()
    document += b" " * (-len(document) % 4)
    glb = (struct.pack("<4sII", b"glTF", 2, 28 + len(document) + len(content))
           + struct.pack("<I4s", len(document), b"JSON") + document
           + struct.pack("<I4s", len(content), b"BIN\0") + content)
    path.write_bytes(glb)


def prepare(render, output):
    document = json.loads((render / "lightbeams.json").read_text(encoding="utf-8"))
    beam = next(entry for entry in document["beams"] if entry["name"] == LIGHT)
    lighting = json.loads((render / "lighting.json").read_text(encoding="utf-8"))["lights"]
    light = next(entry for entry in lighting if entry["name"] == LIGHT)
    if (not beam["lightOn"] or not beam["initiallyOn"] or beam["gameplay"]
            or light.get("triggered") or light["ltype"] != 1 or light["effect"] != 22):
        raise ValueError("The fixture must be the authored steady skylight")

    mesh = document["meshes"][beam["mesh"]]
    raw = np.asarray(mesh["positions"]).reshape(-1, 3)
    triangles = np.asarray(mesh["indices"]).reshape(-1, 3)
    length = beam["echelonRange"] * beam["falloffScale"]
    near = beam["nearPlaneRatio"]
    if not 0 < near < 1 or len(triangles) != 72 or length <= 0:
        raise ValueError("Unexpected light-beam slice mesh or extent")
    grow = near + (1 - near) * beam["falloffScale"] * raw[:, 0]
    frame = axes(beam["rotation"])
    relative = np.column_stack((raw[:, 0] * length, raw[:, 1] * grow * beam["spotWidth"],
                                raw[:, 2] * grow * beam["spotHeight"])) @ frame
    origin = np.asarray(beam["location"])
    world, binary = read_glb(render / "world.glb")
    centre, uv = grate_frame(world, binary)
    # The widening rectangle has the same projection as an apex behind the fixture. This puts the grate
    # at its authored near-window scale instead of a point-light shadow that balloons across the entire hall.
    apex = origin - frame[0] * beam["echelonRange"] * near / (1 - near)
    bounds = np.stack((relative.min(axis=0), relative.max(axis=0)))
    lower, upper = bounds + origin
    upper[2] = centre[2]
    cast_triangles = shadow_triangles(world, binary, render, lower, upper)
    maximum = math.ceil(centre[2] - lower[2] + 2)
    cast_depth = first_cast_depth(cast_triangles, apex, centre, uv, maximum)
    manifest = {"map": "MounE", "light": LIGHT, "mesh": ASSET + ".glb", "transmission": TRANSMISSION,
                "cast_depth": CAST_DEPTH, "cast_depth_range": maximum,
                "origin": origin.tolist(), "forward": frame[0].tolist(), "apex": apex.tolist(),
                "length": length, "grate_centre": centre.tolist(), "grate_u": uv[0].tolist(),
                "grate_v": uv[1].tolist(), "colour": beam["color"],
                "strength": 0.046 * beam["brightness"] / 255.0 * math.sqrt(beam["alpha"]),
                "bounds": bounds.tolist()}
    output.mkdir(parents=True, exist_ok=True)
    with Image.open(render / "textures" / MASK) as source:
        alpha = np.asarray(source.convert("RGBA"))[:, :, 3]
        if alpha.min() != 0 or alpha.max() != 255:
            raise ValueError("Expected clear apertures and opaque metal in the grate")
        Image.fromarray(255 - alpha).save(output / TRANSMISSION)
    Image.fromarray(np.rint(cast_depth * (255.0 / maximum)).astype(np.uint8)).save(output / CAST_DEPTH)
    write_mesh(output / manifest["mesh"], relative, triangles)
    (output / "SideStairsShaft.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{LIGHT}: {len(raw)} source vertices, {len(triangles)} triangles, length {length:g}, grate z={centre[2]:g}")
    print(f"  {len(cast_triangles)} scene triangles, clear at ceiling mouth: {(cast_depth >= centre[2] - 128).mean():.1%}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("render", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.render.resolve().parent.name != "MounE" or args.render.resolve() in args.output.resolve().parents:
        parser.error("Use the installed MounE render folder and a separate output folder")
    prepare(args.render, args.output)
