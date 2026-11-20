"""Project the solid bars of the original upper-hall roof into Light246's shadow texture."""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from prepare_hospital_lightmaps import accessor, read_glb
from prepare_hospital_side_stairs_beam import axes, placement_frame


def prepare(render, output):
    light = next(row for row in json.loads((render / "lighting.json").read_text())["lights"]
                 if row["name"] == "Light246")
    document, binary = read_glb(render / "world.glb")
    roof = next(row for row in document["extras"]["svmap"]["instances"] if row["actor"] == "StaticMeshActor5858")
    if (roof["mesh"].split("#")[0] != "Prison_STC.common.roof1" or roof["hidden"] or roof["gameplay"]
            or light["effect"] != 22 or not 0 < light["near_plane_ratio"] < 1):
        raise ValueError("Unexpected Room 214 roof or projector")
    mesh = next(node["mesh"] for node in document["nodes"] if node.get("name") == roof["mesh"])
    basis = axes(light["rotation"])
    reach = light["echelon_range"]
    behind = reach * light["near_plane_ratio"] / (1.0 - light["near_plane_ratio"])
    apex = np.asarray(light["location"]) - behind * basis[0]
    half_extent = np.asarray([light["spot_width"], light["spot_height"]])
    props = light["props"]
    width = 128 << props["ShadowZBufferWidthType"]
    height = 128 << props["ShadowZBufferHeightType"]
    # Coverage samples at four times the original shadow-map resolution soften sub-texel diagonals.
    size = np.array([width, height]) * 4
    transmission = Image.new("L", tuple(size), 255)
    draw = ImageDraw.Draw(transmission)
    for primitive in document["meshes"][mesh]["primitives"]:
        if document["materials"][primitive["material"]].get("alphaMode", "OPAQUE") != "OPAQUE":
            raise ValueError("This roof needs its alpha texture evaluated as well as its triangles")
        points = accessor(document, binary, primitive["attributes"]["POSITION"], "VEC3", {5126: "<f4"})
        placed = (points[:, [0, 2, 1]] * 100 * roof["scale"]) @ placement_frame(roof) + roof["location"]
        local = (placed - apex) @ basis.T
        if np.any(local[:, 0] <= 0):
            raise ValueError("Roof extends behind the projector apex")
        # M_ScctLightFunction samples +Y across and -Z down in the light's local axes.
        uv = 0.5 + local[:, 1:] / local[:, :1] * (reach + behind) / (2 * half_extent) * [1, -1]
        pixels = np.rint(uv * size - 0.5).astype(int)
        indices = accessor(document, binary, primitive["indices"], "SCALAR", {5123: "<u2", 5125: "<u4"})
        for triangle in pixels[indices.reshape(-1, 3)]:
            draw.polygon([tuple(point) for point in triangle], fill=0)
    output.parent.mkdir(parents=True, exist_ok=True)
    # The shared light function's Cookie is a colour sampler.
    transmission.resize((width, height), Image.Resampling.BOX).convert("RGB").save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("render", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare(args.render, args.output)
