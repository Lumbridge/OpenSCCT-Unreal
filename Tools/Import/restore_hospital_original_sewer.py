"""Restore the installed Mount Hospital drain and water imagery at higher resolution."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

from prepare_hospital_lightmaps import accessor, chart_mask, read_glb


DRAIN = "Prison_TEX_plaquegout.png"
WATER = "Prison_TEX_water.png"
DRAIN_MESH = "Prison_STC.common.egoutplaque"
DRAIN_MATERIAL = "Prison_TEX.plaquegout"
WATER_MATERIAL = "Prison_TEX.water__lm2"
WEIGHTS = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def resample(values, scale, mode, order=3):
    if values.ndim == 2:
        return ndimage.zoom(values, scale, order=order, mode=mode, grid_mode=True)
    return np.stack([resample(channel, scale, mode, order) for channel in np.moveaxis(values, 2, 0)], axis=2)


def smoothstep(low, high, x):
    t = np.clip((x - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)


def coarse_match(source, output, scale, mode):
    height, width = source.shape[:2]
    # The continuous correction limits drift without stamping the source's coarse pixel grid onto the output.
    for _ in range(3):
        area = output.reshape(height, scale, width, scale, 3).mean(axis=(1, 3))
        output += resample(source - area, scale, mode, order=1)
        np.clip(output, 0, 255, out=output)
    return output


def slots_in(luma, scale=1):
    height, width = luma.shape
    y, x = np.ogrid[:height, :width]
    radius = np.hypot((x + 0.5) / scale - 64, (y + 0.5) / scale - 64)
    ids, _ = ndimage.label((luma < 23) & (radius < 57.5))
    sizes = np.bincount(ids.ravel())
    retained = np.flatnonzero(sizes >= 5 * scale * scale)
    retained = retained[retained != 0]
    return np.isin(ids, retained), ndimage.center_of_mass(np.ones_like(luma), ids, retained)


def source_drain_uv(render):
    document, binary = read_glb(render / "world.glb")
    node = next(entry for entry in document["nodes"] if entry.get("name") == DRAIN_MESH)
    primitive, = document["meshes"][node["mesh"]]["primitives"]
    if document["materials"][primitive["material"]]["name"] != DRAIN_MATERIAL:
        raise ValueError("The drain mesh has a different material binding")
    attrs = primitive["attributes"]
    uv = accessor(document, binary, attrs["TEXCOORD_0"], "VEC2", {5126: "<f4"})
    position = accessor(document, binary, attrs["POSITION"], "VEC3", {5126: "<f4"})
    indices = accessor(document, binary, primitive["indices"], "SCALAR", {5123: "<u2", 5125: "<u4"}).reshape(-1, 3)
    triangles = np.column_stack((uv, position))[indices]
    top = np.isclose(triangles[:, :, 3], position[:, 1].max(), atol=0.0001).all(axis=1)
    return triangles[top], triangles[~top]


def match_drain_luma(source, colour, scale, triangles):
    # Correct the rendered UV regions while excluding the slots and unused corners.
    old_luma, new_luma = source @ WEIGHTS, colour @ WEIGHTS
    old_masks = [chart_mask(mesh, old_luma.shape) >= 0 for mesh in triangles]
    new_masks = [chart_mask(mesh, new_luma.shape) >= 0 for mesh in triangles]
    height, width = new_luma.shape
    y, x = np.ogrid[:height, :width]
    radius = np.hypot((x + 0.5) / scale - 64, (y + 0.5) / scale - 64)
    on_metal = smoothstep(26, 42, new_luma)
    rim = smoothstep(56.75, 61.0, radius)
    weights = np.stack((on_metal * (1 - rim), on_metal * rim), axis=2)
    error = np.array([old_luma[m].mean() for m in old_masks]) - np.array([new_luma[m].mean() for m in new_masks])
    gain = np.linalg.solve([weights[m].mean(axis=0) for m in new_masks], error)
    return np.rint(np.clip(colour + (weights @ gain)[..., None], 0, 255)).astype(np.uint8)


def restore_drain(source, scale, triangles):
    mode = "nearest"
    luma = source @ WEIGHTS
    slots, old_centres = slots_in(luma)
    if len(old_centres) != 84:
        raise ValueError("This drain atlas does not contain the 84 original slot outlines")

    # Trace the 84 actual source openings in the source UVs. Do not generate a new pattern or a new border.
    outside, outside_index = ndimage.distance_transform_edt(slots, return_indices=True)
    inside, inside_index = ndimage.distance_transform_edt(~slots, return_indices=True)
    signed = np.where(slots, 0.5 - outside, inside - 0.5).astype(np.float32)
    # Soften source-texel stair steps without moving or adding a slot.
    signed = ndimage.gaussian_filter(signed, 0.62, mode=mode) - 0.10
    signed = resample(signed, scale, mode)
    shape = 1 - smoothstep(-0.24, 0.24, signed)
    boundary = 1 - smoothstep(0.9, 2.25, np.abs(signed))

    inside_rgb = source[tuple(inside_index)]
    metal_rgb = source[tuple(outside_index)]
    metal = resample(metal_rgb, scale, mode)
    well = resample(inside_rgb, scale, mode)
    restored = resample(source, scale, mode)
    edge = well * shape[..., None] + metal * (1 - shape[..., None])
    restored = restored * (1 - boundary[..., None]) + edge * boundary[..., None]

    # Keep the pictured ring, cast wear, and asymmetries from the source while restoring its local contrast.
    luminance = restored @ WEIGHTS
    detail = luminance - ndimage.gaussian_filter(luminance, 1.4, mode=mode)
    restored += np.clip(detail * 0.65, -9, 9)[..., None] * (1 - boundary[..., None])
    colour = match_drain_luma(source, coarse_match(source, restored, scale, mode), scale, triangles)

    # Form normal contours from the same guide as the colour slits. The rest uses restrained original grain.
    height = -0.22 * shape + np.clip(detail, -12, 12) * 0.0014 * (1 - boundary)
    height = ndimage.gaussian_filter(height, 0.9, mode=mode)
    dy, dx = np.gradient(height)
    normal = np.stack((-dx * scale, -dy * scale, np.ones_like(height)), axis=2)
    normal /= np.linalg.norm(normal, axis=2)[..., None]
    normal = np.rint((normal * 0.5 + 0.5) * 255).astype(np.uint8)

    _, centres = slots_in(colour @ WEIGHTS, scale)
    if len(centres) != len(old_centres):
        raise ValueError(f"Drain lost or gained a slot during restoration ({len(centres)} output slots)")
    original_xy = np.array(old_centres)
    restored_xy = (np.array(centres) + 0.5) / scale - 0.5
    displacement = np.linalg.norm(original_xy[:, None, :] - restored_xy[None, :, :], axis=2)
    if np.max(displacement.min(axis=1)) > 0.8 or len(np.unique(displacement.argmin(axis=1))) != len(original_xy):
        raise ValueError("Drain slot alignment no longer follows the source atlas")
    return colour, normal, round(float(np.max(displacement.min(axis=1))), 4)


def restore_water(source, scale):
    mode = "grid-wrap"
    original = resample(source, scale, mode)
    luma = original @ WEIGHTS
    nearby = ndimage.gaussian_filter(luma, 2.0, mode=mode)
    highlights = smoothstep(95, 180, luma)
    # Narrow the source's existing specular streaks and sharpen ripples without moving or adding any.
    delta = np.clip((luma - nearby) * (0.60 + 0.36 * highlights), -11, 16)
    return np.rint(coarse_match(source, original + delta[..., None], scale, mode)).astype(np.uint8)


def read_original(render, filename, size, material):
    materials = json.loads((render / "materials.json").read_text(encoding="utf-8"))["materials"]
    flags = materials[material]["flags"]
    if flags.get("texture") != "textures/" + filename or flags.get("masked") or flags.get("translucent"):
        raise ValueError(f"Unexpected source material flags for {material}")
    path = render / "textures" / filename
    with Image.open(path) as image:
        if image.size != (size, size) or image.convert("RGBA").getchannel("A").getextrema() != (255, 255):
            raise ValueError(f"Unexpected source size or alpha for {filename}")
        return np.asarray(image.convert("RGB"), dtype=np.float32), hashlib.sha256(path.read_bytes()).hexdigest()


def check_luma(source, result, regions):
    before, after = source @ WEIGHTS, result @ WEIGHTS
    metrics = {}
    for name, triangles in regions.items():
        original = before if triangles is None else before[chart_mask(triangles, before.shape) >= 0]
        restored = after if triangles is None else after[chart_mask(triangles, after.shape) >= 0]
        values = [round(float(original.mean()), 4), round(float(restored.mean()), 4)]
        if abs(values[1] - values[0]) > 0.15:
            raise ValueError(f"{name} luminance changed from {values[0]} to {values[1]}")
        metrics[name + "_mean_luma_codes_before_after"] = values
    return metrics


def run(render, output):
    drain, drain_hash = read_original(render, DRAIN, 128, DRAIN_MATERIAL)
    water, water_hash = read_original(render, WATER, 256, WATER_MATERIAL)
    face, rim = source_drain_uv(render)
    restored_drain, normal, displacement = restore_drain(drain, 8, (face, rim))
    restored_water = restore_water(water, 4)
    normal_name = Path(DRAIN).stem + "_N.png"
    manifest = {"map": "MounE", "textures": {
        DRAIN: {"material": DRAIN_MATERIAL, "source_sha256": drain_hash, "normal": normal_name,
                "slots": 84, "max_slot_centroid_shift_source_texels": displacement,
                **check_luma(drain, restored_drain, {"face": face, "rim": rim})},
        WATER: {"material": WATER_MATERIAL, "source_sha256": water_hash,
                **check_luma(water, restored_water, {"tile": None}),
                "visible_quarter_mean_luma_codes_before_after": [
                    round(float((pixels[-pixels.shape[0] // 4:] @ WEIGHTS).mean()), 4)
                    for pixels in (water, restored_water)]},
    }}
    output.mkdir(parents=True, exist_ok=True)
    for name, values in ((DRAIN, restored_drain), (normal_name, normal), (WATER, restored_water)):
        Image.fromarray(values).save(output / name)
    (output / "original-sewer.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("render", type=Path, help="Installed converted MounE/render folder")
    parser.add_argument("output", type=Path, help="Staging folder")
    args = parser.parse_args()
    render, output = args.render.resolve(), args.output.resolve()
    if render.parent.name != "MounE" or output == render.parent or render.parent in output.parents:
        parser.error("Pass the MounE render folder and stage the output outside the installed map")
    run(render, output)


if __name__ == "__main__":
    main()
