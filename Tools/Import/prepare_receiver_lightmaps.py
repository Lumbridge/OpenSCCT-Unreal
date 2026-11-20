"""Subtract overlapping direct illumination from a map's existing UV1 lightmap pages.

Run as external Python (NumPy and Pillow) through scct_receiver_lightmaps.py. The job
lists the exact source textures and static lights selected in the prepared Unreal level.
Optional embreex accelerates the same finite visibility rays used by the NumPy fallback.
"""
import argparse
import json
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image

from prepare_rain_cover import accessor, read_glb, world_triangles


def triangle_tree(triangles):
    lower = triangles.min(axis=1)
    upper = triangles.max(axis=1)
    center = (lower + upper) * 0.5

    def split(indices):
        box = (lower[indices].min(axis=0), upper[indices].max(axis=0))
        if len(indices) <= 12:
            return (*box, triangles[indices], None)
        axis = np.ptp(center[indices], axis=0).argmax()
        middle = len(indices) // 2
        indices = indices[np.argpartition(center[indices, axis], middle)]
        return (*box, None, (split(indices[:middle]), split(indices[middle:])))

    return split(np.arange(len(triangles)))


def make_visibility(triangles):
    """Tests finite rays from a light to its receivers, stopping just before the receiver."""
    try:
        from embreex.mesh_construction import TriangleMesh
        from embreex.rtcore_scene import EmbreeScene
    except ImportError:
        tree = triangle_tree(triangles)
    else:
        scene = EmbreeScene()
        mesh = TriangleMesh(scene, np.ascontiguousarray(triangles, dtype=np.float32))

        def accelerated(origin, directions, distances):
            starts = np.broadcast_to(origin.astype(np.float32), directions.shape).copy()
            depth = scene.run(starts, directions.astype(np.float32), query="DISTANCE")
            return depth >= distances - 0.35

        return accelerated

    def visible(origin, directions, distances):
        clear = np.ones(len(directions), dtype=bool)
        inv = 1.0 / np.where(np.abs(directions) < 1e-15, 1e-15, directions)

        def visit(node, ids):
            lo, hi, faces, children = node
            ids = ids[clear[ids]]
            a, b = (lo - origin) * inv[ids], (hi - origin) * inv[ids]
            near = np.maximum(0.0, np.minimum(a, b).max(axis=1))
            far = np.minimum(distances[ids] - .35, np.maximum(a, b).min(axis=1))
            ids = ids[far >= near]
            if not len(ids):
                return
            if children:
                for child in children:
                    visit(child, ids)
                return
            edge1, edge2 = faces[:, 1] - faces[:, 0], faces[:, 2] - faces[:, 0]
            start = origin - faces[:, 0]
            cross = np.cross(start, edge1)
            h = np.cross(directions[ids, None], edge2[None])
            det = np.einsum("rti,ti->rt", h, edge1)
            reciprocal = np.divide(1., det, out=np.zeros_like(det), where=np.abs(det) > 1e-9)
            u = np.einsum("rti,ti->rt", h, start) * reciprocal
            v = (directions[ids] @ cross.T) * reciprocal
            depth = np.einsum("ti,ti->t", edge2, cross) * reciprocal
            hit = (u >= -1e-7) & (v >= -1e-7) & (u + v <= 1.0000001)
            hit &= (depth > 0.0) & (depth < distances[ids, None] - .35)
            clear[ids[hit.any(axis=1)]] = False

        for offset in range(0, len(clear), 4096):
            visit(tree, np.arange(offset, min(offset + 4096, len(clear))))
        return clear

    return visible


def world_receivers(doc, binary, eligible):
    result = {}
    for node in doc["nodes"]:
        role = node.get("extras", {}).get("svmap", {})
        if role.get("role") != "bsp" or role.get("sky") or "mesh" not in node:
            continue
        for prim in doc["meshes"][node["mesh"]]["primitives"]:
            material = doc["materials"][prim["material"]]
            page = material.get("extras", {}).get("scct_lightmap")
            if not page or re.sub(r"[^a-z0-9_]", "_", material["name"].casefold()) not in eligible:
                continue
            attr = prim["attributes"]
            pos = accessor(doc, binary, attr["POSITION"])[:, [0, 2, 1]] * 100.
            normal = accessor(doc, binary, attr["NORMAL"])[:, [0, 2, 1]]
            uv = accessor(doc, binary, attr["TEXCOORD_1"])
            ids = accessor(doc, binary, prim["indices"]).reshape(-1, 3)
            result.setdefault(page, []).extend(zip(uv[ids], pos[ids], normal[ids]))
    return result


def rasterize(faces, size):
    width, height = size
    position = np.zeros((height, width, 3), np.float32)
    normal = np.zeros_like(position)
    ownership = np.zeros((height, width), np.int8)
    for uv, xyz, norms in faces:
        a, b, c = uv.astype(float) * size
        x0, y0 = np.maximum(np.ceil(np.min((a, b, c), axis=0) - .5).astype(int), 0)
        x1, y1 = np.minimum(np.floor(np.max((a, b, c), axis=0) - .5).astype(int) + 1, size)
        det = (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])
        if abs(det) < 1e-8 or x0 >= x1 or y0 >= y1:
            continue
        y, x = np.mgrid[y0:y1, x0:x1]
        dx, dy = x + .5 - a[0], y + .5 - a[1]
        u = (dx*(c[1]-a[1]) - dy*(c[0]-a[0])) / det
        v = (dy*(b[0]-a[0]) - dx*(b[1]-a[1])) / det
        take = (u >= -1e-7) & (v >= -1e-7) & (u+v <= 1.0000001)
        x, y = x[take], y[take]
        weights = np.stack([1.-u[take]-v[take], u[take], v[take]], axis=1)
        points, normals = weights @ xyz, weights @ norms
        old = ownership[y, x]
        conflict = (old != 0) & ((np.linalg.norm(points-position[y,x],axis=1) > .1)
                                  | (np.sum(normals*normal[y,x],axis=1) < .99))
        ownership[y, x] = np.where(conflict | (old < 0), -1, 1)
        position[y, x], normal[y, x] = points, normals
    valid = ownership == 1
    n = normal[valid]
    n /= np.maximum(np.linalg.norm(n,axis=1,keepdims=True), 1e-8)
    return valid, position[valid], n


def zone_query(render):
    """Use the same SVCL 1.0 world BSP walk as CollisionWorld.GetZoneIndex."""
    filename = render.parent / "sim/collision.bin"
    if not filename.is_file():
        return None
    data = filename.read_bytes()
    if data[:4] != b"SVCL" or struct.unpack_from("<H", data, 4)[0] != 1:
        return None
    chunks = {}
    cursor = 16
    for _ in range(struct.unpack_from("<I", data, 8)[0]):
        kind, version, size, count = struct.unpack_from("<4sIII", data, cursor)
        if version == 1:
            chunks[kind] = (cursor + 16, size, count)
        cursor = (cursor + 16 + size + 15) & ~15
    if b"BSPN" not in chunks or b"COLL" not in chunks:
        return None
    start, size, count = chunks[b"BSPN"]
    if size != count * 48 or not count:
        return None
    planes = np.ndarray((count, 4), '<f4', data, start, (48, 4))
    children = np.ndarray((count, 2), '<i4', data, start + 16, (48, 4))
    zones = np.ndarray((count, 2), 'u1', data, start + 42, (48, 1))
    start, size, count = chunks[b"COLL"]
    if size != count * 128:
        return None
    root = next((struct.unpack_from('<I', data, start + 128*i + 32)[0] for i in range(count)
                 if data[start + 128*i] == 0 and struct.unpack_from('<I', data, start + 128*i + 36)[0]), -1)
    if root < 0:
        return None

    def query(points):
        output = np.full(len(points), -1, dtype=np.int16)
        indices = np.arange(len(points))
        node = np.full(len(points), root, dtype=np.int32)
        for _ in range(len(planes)):
            if not len(indices):
                break
            plane = planes[node]
            back = (np.einsum('ij,ij->i', points[indices], plane[:, :3]) < plane[:, 3]).astype(np.intp)
            child = children[node, back]
            terminal = child < 0
            output[indices[terminal]] = zones[node[terminal], back[terminal]]
            indices, node = indices[~terminal], child[~terminal]
        return output

    return query


def estimate(fixtures, positions, normals, visible, region, zone_numbers):
    best = np.zeros(len(positions), np.float32)
    # The BSP triangle itself is a zone boundary. Sample just inside the lit room.
    zones = region(positions + normals * .5) if region and any("zone" in lamp for lamp in fixtures) else None
    for lamp in sorted(fixtures, key=lambda row: row["name"].casefold()):
        origin = np.array(lamp["position"], float)
        rays = positions - origin
        distances = np.linalg.norm(rays, axis=1)
        within = (distances > 1.) & (distances < lamp["radius"])
        if "zone" in lamp:
            zone = zone_numbers.get(lamp["zone"].casefold())
            if zones is None or zone is None:
                continue
            within &= zones == zone
        ids = np.flatnonzero(within)
        ray = rays[ids] / distances[ids,None]
        incidence = np.maximum(0., -np.einsum("ij,ij->i", normals[ids], ray))
        radial = np.maximum(0., 1.-distances[ids]/lamp["radius"]) ** 2
        score = lamp["strength"] * incidence * radial
        if "direction" in lamp:
            cosine = ray @ np.array(lamp["direction"])
            inner, outer = np.cos(np.deg2rad([lamp["inner"], lamp["outer"]]))
            score *= np.clip((cosine - outer) / max(inner - outer, 1e-5), 0., 1.) ** 2
        # Use the strongest supported light once. Cap the possible reduction in linear light.
        share = .25 * score / (.25 + score)
        active = np.flatnonzero((share > .002) & (share > best[ids]))
        if len(active):
            accepted = active[visible(origin, ray[active], distances[ids[active]])]
            best[ids[accepted]] = share[accepted]
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("job", type=Path)
    path = parser.parse_args().job
    job = json.loads(path.read_text(encoding="utf-8"))
    render = Path(job["render"])
    source_lighting = json.loads((render / "lighting.json").read_text(encoding="utf-8"))
    zone_numbers = {zone["name"].casefold(): zone["zoneIndex"] for zone in source_lighting.get("zones", []) if "zoneIndex" in zone}
    region = zone_query(render)
    document, binary = read_glb(render / "world.glb")
    receivers = world_receivers(document, binary, set(job["receivers"]))
    # Including opaque props and closed static geometry in LOS also protects receivers
    # behind props. Extra geometry in the blocker set only leaves more source fill intact.
    triangles = world_triangles(document, binary)
    visible = make_visibility(triangles)
    for item in job["textures"]:
        original = Image.open(item["source"])
        rgb = np.array(original.convert("RGB"))
        original_size = Image.open(render/item["page"]).size
        valid, positions, normals = rasterize(receivers.get(item["page"], ()), original_size)
        reduction = np.zeros(valid.shape, np.float32)
        reduction[valid] = estimate(job["fixtures"], positions, normals, visible, region, zone_numbers)
        if original.size != original_size:
            reduction = np.asarray(Image.fromarray(reduction).resize(original.size, Image.Resampling.NEAREST))
        # Keep original near-black pockets intact and taper the adjustment in its penumbra.
        lightness = rgb @ np.array([.2126, .7152, .0722])
        lit = np.clip((lightness - 36.) / 56., 0., 1.)
        reduction = reduction * lit * lit * (3. - 2.*lit)
        # In the original lighting graph the atlas is decoded back to authored display codes
        # and raised to 2.2. Apply a single linear-light attenuation, keeping RGB ratios.
        output = np.rint(rgb * np.power(1. - reduction[:, :, None], 1./2.2)).astype(np.uint8)
        image = Image.fromarray(output)
        if "A" in original.getbands():
            image.putalpha(original.getchannel("A"))
        image.save(item["output"])
        changed = np.any(output != rgb, axis=2)
        if np.any(output > rgb) or np.any(changed & (reduction == 0)):
            raise ValueError("Residual changed an uncovered texel or increased baked illumination")
        print(f"{item['page']}: {int(changed.sum())} changed pixels, max linear reduction {reduction.max():.3f}")
    return 0


if __name__ == "__main__":
    main()
