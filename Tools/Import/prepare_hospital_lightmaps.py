"""Prepare optional Enhanced Mount Hospital lightmaps from the converted map's own UV1 charts."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image


PAGES = tuple(f"textures/MounE_lightmap{page}.png" for page in range(3))
WEIGHTS = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
EMPTY = -1
OVERLAP = -2
PNG_ENCODING = np.arange(256, dtype=np.float32) / 255.0
PNG_LINEAR = np.where(PNG_ENCODING <= 0.04045, PNG_ENCODING / 12.92,
                      ((PNG_ENCODING + 0.055) / 1.055) ** 2.4)


def read_glb(path):
    data = path.read_bytes()
    if struct.unpack_from("<4sII", data) != (b"glTF", 2, len(data)):
        raise ValueError(f"Invalid glTF 2.0 container: {path}")
    length, kind = struct.unpack_from("<I4s", data, 12)
    document_end = 20 + length
    binary_length, binary_kind = struct.unpack_from("<I4s", data, document_end)
    if kind != b"JSON" or binary_kind != b"BIN\0" or document_end + 8 + binary_length != len(data):
        raise ValueError(f"Expected a JSON chunk and an embedded buffer: {path}")
    return json.loads(data[20:document_end]), memoryview(data)[document_end + 8:]


def accessor(document, binary, index, shape, component_types):
    entry = document["accessors"][index]
    view = document["bufferViews"][entry["bufferView"]]
    number = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[shape]
    if (entry["type"] != shape or entry["componentType"] not in component_types
            or view["buffer"] != 0 or "sparse" in entry):
        raise ValueError(f"Unsupported {shape} accessor {index}")
    dtype = np.dtype(component_types[entry["componentType"]])
    stride = view.get("byteStride", number * dtype.itemsize)
    offset = view.get("byteOffset", 0) + entry.get("byteOffset", 0)
    return np.ndarray((entry["count"], number), dtype=dtype, buffer=binary,
                      offset=offset, strides=(stride, dtype.itemsize))


def material_key(name):
    # Match the importer's punctuation-insensitive material name lookup.
    return re.sub(r"[^a-z0-9]", "", name.lower())


def page_triangles(render):
    materials = json.loads((render / "materials.json").read_text(encoding="utf-8"))
    bindings = {entry["name"]: entry["lightmap"] for entry in materials["lightmapped"]}
    painted_wall = {material_key(entry["name"]) for entry in materials["lightmapped"]
                    if entry["base"].casefold() == "prison_tex.wallfloor1"}
    if set(bindings.values()) != set(PAGES):
        raise ValueError("Expected the three Enhanced Mount Hospital baked pages")

    document, binary = read_glb(render / "world.glb")
    triangles = {page: [] for page in PAGES}
    walls = {page: [] for page in PAGES}
    gltf_bindings = {material_key(name): page for name, page in bindings.items()}
    seen = set()
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            material = document["materials"][primitive["material"]]
            page = material.get("extras", {}).get("scct_lightmap")
            if page is None:
                continue
            key = material_key(material["name"])
            if gltf_bindings.get(key) != page or primitive.get("mode", 4) != 4:
                raise ValueError(f"Unexpected lightmapped primitive in {mesh.get('name', 'world.glb')}")
            seen.add(key)
            attributes = primitive["attributes"]
            uv = accessor(document, binary, attributes["TEXCOORD_1"], "VEC2", {5126: "<f4"})
            xyz = accessor(document, binary, attributes["POSITION"], "VEC3", {5126: "<f4"})
            indices = accessor(document, binary, primitive["indices"], "SCALAR",
                               {5121: "u1", 5123: "<u2", 5125: "<u4"}).reshape(-1, 3)
            faces = np.column_stack((uv, xyz))[indices]
            triangles[page].append(faces)
            normal = np.cross(faces[:, 1, 2:] - faces[:, 0, 2:], faces[:, 2, 2:] - faces[:, 0, 2:])
            area = np.linalg.norm(normal, axis=1)
            # glTF is Y-up. The painted wall texture also lines horizontal ledges.
            # Degenerate triangles carry no wall texels and cannot disqualify their chart.
            walls[page].append((area == 0) | ((key in painted_wall) & (np.abs(normal[:, 1]) <= area * 0.02)))

    if seen != gltf_bindings.keys():
        raise ValueError("A material binding has no matching lightmapped primitive")
    return (bindings, {page: np.concatenate(parts) for page, parts in triangles.items()},
            {page: np.concatenate(parts) for page, parts in walls.items()})


def connected_triangles(triangles):
    """Weld edges in both chart and geometry space to avoid joining neighboring packed islands."""
    parents = np.arange(len(triangles), dtype=np.int32)

    def root(index):
        while parents[index] != index:
            parents[index] = parents[parents[index]]
            index = parents[index]
        return index

    owners = {}
    keys = np.rint(triangles * 100000).astype(np.int64)
    for index, vertices in enumerate(keys):
        a, b, c = map(tuple, vertices)
        for start, end in ((a, b), (b, c), (c, a)):
            if start == end:
                continue
            edge = (start, end) if start < end else (end, start)
            previous = owners.setdefault(edge, index)
            if previous != index:
                parents[root(index)] = root(previous)
    return np.unique([root(index) for index in range(len(triangles))], return_inverse=True)[1]


def chart_mask(triangles, shape, labels=None):
    height, width = shape
    vertices = triangles.astype(np.float64)
    vertices[:, :, :2] *= [width, height]
    charts = connected_triangles(vertices) if labels is None else labels
    mask = np.full(shape, EMPTY, dtype=np.int32)
    for uv, chart in zip(vertices[:, :, :2], charts, strict=True):
        min_x, min_y = np.maximum(np.ceil(uv.min(axis=0) - 0.5).astype(int), 0)
        max_x, max_y = np.minimum(np.floor(uv.max(axis=0) - 0.5).astype(int) + 1, [width, height])
        a, b, c = uv
        orient = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(orient) < 1e-8 or min_x >= max_x or min_y >= max_y:
            continue
        x = np.arange(min_x, max_x) + 0.5
        y = np.arange(min_y, max_y)[:, None] + 0.5
        inside = np.ones((len(y), len(x)), dtype=bool)
        for first, second in ((a, b), (b, c), (c, a)):
            edge = (second[0] - first[0]) * (y - first[1]) - (second[1] - first[1]) * (x - first[0])
            inside &= edge * np.sign(orient) >= -1e-7
        region = mask[min_y:max_y, min_x:max_x]
        unclaimed = region == EMPTY
        region[inside & (region != chart) & ~unclaimed] = OVERLAP
        region[inside & unclaimed] = chart
    return mask


def to_codes(value):
    return 255.0 * np.where(value <= 0.0031308, 12.92 * value, 1.055 * value ** (1.0 / 2.4) - 0.055)


def to_linear(codes):
    value = codes / 255.0
    return np.where(value <= 0.04045, value / 12.92, ((value + 0.055) / 1.055) ** 2.4)


def ramp(low, high, value):
    fraction = np.clip((value - low) / (high - low), 0, 1)
    return fraction ** 2 * (3 - 2 * fraction)


def aligned_slices(length, offset):
    return slice(max(-offset, 0), min(length, length - offset)), slice(max(offset, 0), min(length, length + offset))


def smooth_chroma(luma, chroma, charts):
    # Color can use a wider range than luminance.
    chroma_sum = chroma.copy()
    chroma_weight = np.ones_like(luma)
    height, width = luma.shape
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            if dx == 0 and dy == 0:
                continue
            y, near_y = aligned_slices(height, dy)
            x, near_x = aligned_slices(width, dx)
            dest, source = (y, x), (near_y, near_x)
            same_chart = (charts[dest] >= 0) & (charts[dest] == charts[source])
            change = luma[source] - luma[dest]
            spatial = np.exp(-(dx * dx + dy * dy) / 5.12)
            chroma_mix = (spatial * np.exp(-0.5 * (change / 12.0) ** 2)) * same_chart * (luma[source] > 2)
            chroma_sum[dest] += chroma[source] * chroma_mix[..., None]
            chroma_weight[dest] += chroma_mix
    return chroma_sum / chroma_weight[..., None]


def smooth_luma(codes, charts, editable):
    # Exchange light between adjacent texels within each chart. Fixed symmetric weights retain
    # the mean and stop light from filtering across silhouettes, gaps, or bright patches.
    height, width = codes.shape
    shadow = 1.0 - ramp(22, 71, codes)
    neighbors = []
    for dy, dx in ((0, 1), (1, 0)):
        y, near_y = aligned_slices(height, dy)
        x, near_x = aligned_slices(width, dx)
        first, second = (y, x), (near_y, near_x)
        valid = editable[first] & editable[second] & (charts[first] == charts[second])
        a, b = codes[first][valid], codes[second][valid]
        labels = charts[first][valid]
        gradient = a - b
        tone = np.minimum(shadow[first][valid], shadow[second][valid])
        energy = np.bincount(labels, weights=tone * np.minimum(gradient ** 2, 64),
                             minlength=int(charts.max()) + 1)
        count = np.bincount(labels, minlength=energy.size)
        mean_energy = np.divide(energy, count, out=np.zeros_like(energy), where=count > 0)
        scale = 3.7 + 0.055 * np.minimum(a, b)
        weight = 0.22 * tone * np.exp(-0.5 * (gradient / scale) ** 2)
        neighbors.append((first, second, valid, weight, labels, mean_energy))

    # Prefer smoothing along the penumbra. The direction with the larger gradient carries
    # more of the actual light falloff, such as the vertical falloff above an opening.
    energy_ratio = (neighbors[0][-1] + 0.3) / (neighbors[1][-1] + 0.3)
    exchanges = []
    for axis, (first, second, valid, weight, labels, _) in enumerate(neighbors):
        direction = energy_ratio if axis == 0 else 1.0 / energy_ratio
        weight *= np.clip(direction[labels] ** -0.70, 0.10, 1.0)
        exchanges.append((first, second, valid, weight))

    current = codes.copy()
    for _ in range(44):
        delta = np.zeros_like(codes)
        for first, second, valid, weight in exchanges:
            change = (current[second][valid] - current[first][valid]) * weight
            delta[first][valid] += change
            delta[second][valid] -= change
        current += delta
    return current


def keep_chart_luma(delta, charts, editable):
    # Limit both directions symmetrically in aggregate so a chart does not get lighter or darker.
    change, labels = delta[editable], charts[editable]
    positive = np.bincount(labels, np.maximum(change, 0))
    negative = np.bincount(labels, np.maximum(-change, 0))
    shared = np.minimum(positive, negative)
    divisor = np.where(change >= 0, positive[labels], negative[labels])
    delta[editable] = change * np.divide(shared[labels], divisor, out=np.zeros_like(divisor), where=divisor > 0)


def clean(rgb, charts):
    linear = PNG_LINEAR[rgb]
    luma = linear @ WEIGHTS
    codes = to_codes(luma)
    real_black = np.all(rgb == 0, axis=2)
    editable = (charts >= 0) & ~real_black & (codes < 91)
    chroma = linear / np.maximum(luma[..., None], 1e-9) - 1.0
    average = smooth_luma(codes, charts, editable)
    nearby_chroma = smooth_chroma(codes, chroma, charts)

    shadow = 1.0 - ramp(17, 91, codes)
    limit = np.minimum(6.0, 0.55 * codes)
    movement = np.clip(average - codes, -limit, limit)
    keep_chart_luma(movement, charts, editable)
    new_luma = to_linear(codes + movement)

    # Keep the sustained red-to-yellow cast from warm lamps as their spill falls off into shadow.
    red, green, blue = np.moveaxis(nearby_chroma, 2, 0)
    warm = ramp(0.02, 0.25, red - green) * ramp(0.02, 0.25, green - blue) * ramp(12, 32, codes)
    mix = shadow * (0.58 - 0.43 * warm)
    attenuation = shadow * (0.69 - 0.58 * warm)
    new_chroma = (chroma + (nearby_chroma - chroma) * mix[..., None]) * (1.0 - attenuation[..., None])
    changed = np.rint(to_codes(np.clip(new_luma[..., None] * (1.0 + new_chroma), 0, 1))).astype(np.uint8)
    # Rounding may push an almost-black color down one code, but must never create new true black.
    keep = editable & np.any(changed != 0, axis=2)
    output = rgb.copy()
    output[keep] = changed[keep]
    return output


def refine_wall_chart(output, source, charts, wall):
    linear = PNG_LINEAR[output]
    luma = linear @ WEIGHTS
    codes = to_codes(luma)
    samples = wall & (codes > 0)
    editable = samples & (codes < 30)
    if np.count_nonzero(samples) < 96 or np.count_nonzero(editable) < 32:
        return False
    deviation = 3.5 if np.percentile(codes[samples], 90) <= 12 else 2.5
    size = np.arange(-int(4 * deviation), int(4 * deviation) + 1)
    kernel = np.exp(-0.5 * (size / deviation) ** 2)
    kernel /= kernel.sum()
    radius = len(kernel) // 2

    def blur(image):
        for axis in (0, 1):
            image = np.apply_along_axis(
                lambda values: np.convolve(np.pad(values, radius), kernel, mode="valid"), axis, image)
        return image

    # Missing UV1 texels (e.g. the doorway itself) and true-black source texels
    # carry no samples. Fade out at bright texels and strong source edges.
    trend = blur(codes * samples) / np.maximum(blur(samples.astype(np.float32)), 1e-8)
    raw = to_codes(PNG_LINEAR[source] @ WEIGHTS)
    gradient = np.zeros_like(raw)
    for axis in (0, 1):
        first, second = ((slice(None, -1), slice(None)), (slice(1, None), slice(None))) if axis == 0 else (
            (slice(None), slice(None, -1)), (slice(None), slice(1, None)))
        edge = np.abs(raw[first] - raw[second]) * (samples[first] & samples[second])
        np.maximum(gradient[first], edge, out=gradient[first])
        np.maximum(gradient[second], edge, out=gradient[second])
    amount = np.clip((30 - codes) / 12, 0, 1) * (1 - ramp(5, 10, gradient))
    movement = np.clip((trend - codes) * amount, -np.minimum(4, 0.55 * codes), 4)
    adjusted = np.clip(codes + movement, raw - 5.5, raw + 5.5)
    change = to_linear(np.maximum(adjusted, 0)) - luma
    change[~editable] = 0
    keep_chart_luma(change, charts, editable)
    scale = (luma + change) / np.maximum(luma, 1e-9)
    adjusted = np.rint(to_codes(np.clip(linear * scale[..., None], 0, 1))).astype(np.uint8)
    keep = editable & np.any(adjusted != 0, axis=2)
    changed = np.any(output[keep] != adjusted[keep])
    output[keep] = adjusted[keep]
    return changed


def refine_walls(output, source, charts, labels, wall_faces):
    """Smooth small shadow steps only on charts wholly backed by the painted vertical walls."""
    wall_labels = np.setdiff1d(labels[wall_faces], labels[~wall_faces])
    refined = 0
    height, width = charts.shape
    for label in wall_labels:
        y, x = np.where(charts == label)
        if len(y) < 96:
            continue
        margin = 14
        section = (slice(max(0, y.min() - margin), min(height, y.max() + margin + 1)),
                   slice(max(0, x.min() - margin), min(width, x.max() + margin + 1)))
        section_charts = charts[section]
        refined += refine_wall_chart(output[section], source[section], section_charts, section_charts == label)
    return refined


def paired_metrics(before, after, charts):
    covered = charts >= 0
    source_linear = PNG_LINEAR[before] @ WEIGHTS
    updated_linear = PNG_LINEAR[after] @ WEIGHTS
    source = to_codes(source_linear)
    updated = to_codes(updated_linear)
    delta = updated - source
    black = np.all(before == 0, axis=2)
    dark = covered & ~black & (source < 28)
    lit = source >= 91
    sizes = np.bincount(charts[covered])
    summed_delta = np.bincount(charts[covered], delta[covered])
    chart_luma_delta = summed_delta[sizes >= 16] / sizes[sizes >= 16]
    if (np.any(before[~covered | lit] != after[~covered | lit])
            or np.any(black != np.all(after == 0, axis=2))
            or np.max(np.abs(delta[covered])) > 6.6 or np.abs(delta[covered].mean()) > 0.08
            or np.max(np.abs(chart_luma_delta)) > 0.32):
        raise ValueError("Output exceeded the lightmap's shade or chart preservation limits")

    # Mean first derivative across only small dark steps. Exclude different charts and real black silhouettes.
    def step(axis):
        first = (slice(None, -1), slice(None)) if axis == 0 else (slice(None), slice(None, -1))
        second = (slice(1, None), slice(None)) if axis == 0 else (slice(None), slice(1, None))
        old = np.abs(source[first] - source[second])
        new = np.abs(updated[first] - updated[second])
        same_chart = charts[first] == charts[second]
        valid = same_chart & dark[first] & dark[second] & (old <= 6)
        return old[valid], new[valid]

    old_x, new_x = step(1)
    old_y, new_y = step(0)
    red, green, blue = np.moveaxis(before.astype(np.int16), 2, 0)
    warm = covered & (source >= 34) & ~lit & (red - green >= 5) & (green - blue >= 5)
    return {
        "charts_with_unambiguous_texels": int(np.unique(charts[covered]).size),
        "covered_texels": int(covered.sum()),
        "overlap_texels_unmodified": int((charts == OVERLAP).sum()),
        "padding_texels_unmodified": int((charts == EMPTY).sum()),
        "changed_texels": int(np.any(before != after, axis=2).sum()),
        "true_black_texels_unchanged": int(black.sum()),
        "bright_texels_unchanged": int(lit.sum()),
        "mean_luminance_change_codes": round(float(delta[covered].mean()), 4),
        "mean_linear_luminance_change": round(float((updated_linear[covered] - source_linear[covered]).mean()), 8),
        "mean_abs_luminance_change_codes": round(float(np.abs(delta[covered]).mean()), 4),
        "p99_abs_luminance_change_codes": round(float(np.percentile(np.abs(delta[covered]), 99)), 4),
        "max_abs_luminance_change_codes": round(float(np.abs(delta[covered]).max()), 4),
        "max_chart_mean_luminance_change_codes_at_least_16_texels": round(float(np.abs(chart_luma_delta).max()), 4),
        "dark_rgb_span_codes_before_after": [round(float(np.ptp(image[dark], axis=1).mean()), 4) for image in (before, after)],
        "dark_step_codes_before_after": [round(float(np.concatenate(values).mean()), 4)
                                         for values in ((old_x, old_y), (new_x, new_y))],
        "warm_midtone_rgb_span_codes_before_after": [round(float(np.ptp(image[warm], axis=1).mean()), 4)
                                                      for image in (before, after)],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("render", type=Path, help="Converted MounE/render folder")
    parser.add_argument("output", type=Path, help="Separate output folder for the three PNGs and manifest")
    args = parser.parse_args()
    render, output = args.render.resolve(), args.output.resolve()
    map_folder = render.parent
    if map_folder.name != "MounE" or output == map_folder or map_folder in output.parents or output in map_folder.parents:
        parser.error("Provide the MounE render folder and a separate output folder")
    bindings, triangles, walls = page_triangles(render)
    prepared = []
    for page, faces in triangles.items():
        path = render / page
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        with Image.open(path) as image:
            if image.mode != "RGB" or image.size != (512, 512):
                raise ValueError(f"Unexpected source image format: {path}")
            source = np.array(image)
        labels = connected_triangles(faces * [source.shape[1], source.shape[0], 1, 1, 1])
        charts = chart_mask(faces, source.shape[:2], labels)
        processed = clean(source, charts)
        wall_count = refine_walls(processed, source, charts, labels, walls[page])
        metrics = paired_metrics(source, processed, charts)
        metrics["painted_wall_charts_refined"] = int(wall_count)
        name = path.stem + "_Refined.png"
        prepared.append((page, processed, {"file": name, "source_sha256": digest, **metrics}))

    output.mkdir(parents=True, exist_ok=True)
    pages = {}
    for page, image, metrics in prepared:
        name = metrics["file"]
        Image.fromarray(image).save(output / name)
        pages[page] = metrics
        print(f"{name} {metrics['changed_texels']} changed texels, "
              f"mean luminance delta {metrics['mean_luminance_change_codes']:+.4f}/255")
    manifest = {"map": "MounE", "pages": pages, "materials": bindings}
    (output / "lightmap-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
