"""Downloads the CC0 assets listed in pbr_sets.json and packs them for the Unreal import.

Run with the system Python (needs numpy and Pillow), as `python -I fetch_assets.py`:

    python -I fetch_assets.py <project>/Saved/Downloads <project>/Saved/ScctImport/ThirdParty

Each archive goes into its own folder under the downloads folder and is treated as data only: nothing in it is
run, and only the image members this script names are read out of it.

A PBR set becomes two files the surface material samples world-aligned as a DETAIL layer under the original art:

  T_<Set>_N.jpg    the set's DirectX normal map, as downloaded.
  T_<Set>_ORD.jpg  R = ambient occlusion,
                   G = roughness moved so its mean is 0.5 (the surface class decides how rough a surface is; the
                       set only adds the variation: scratches, pores, smears),
                   B = the colour map's brightness divided by its mean, times 0.5 (0.5 = no change), so a detail
                       layer can add grain to the original colour without replacing its hue.

An HDRI is saved as downloaded (<name>.hdr) plus a small copy (<name>_light.hdr) for the sky light.
"""
import io
import json
import os
import sys
import urllib.request
import zipfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
# Sent with every request. Nothing about this machine or its user goes out.
USER_AGENT = "asset-fetch/1.0"


def download(url, target):
    if os.path.exists(target) and os.path.getsize(target) > 0:
        return
    os.makedirs(os.path.dirname(target), exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response, open(target + ".part", "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    os.replace(target + ".part", target)


def read_member(archive, suffix):
    """The image inside the archive whose name ends with `suffix`, or None. Read in memory, never extracted."""
    for info in archive.infolist():
        if info.filename.endswith(suffix) and "/" not in info.filename and "\\" not in info.filename:
            return Image.open(io.BytesIO(archive.read(info)))
    return None


def pack_set(name, zip_path, out):
    normal_path = os.path.join(out, "T_%s_N.jpg" % name)
    packed_path = os.path.join(out, "T_%s_ORD.jpg" % name)
    if os.path.exists(normal_path) and os.path.exists(packed_path):
        return
    with zipfile.ZipFile(zip_path) as archive:
        normal = read_member(archive, "_NormalDX.jpg")
        roughness = read_member(archive, "_Roughness.jpg")
        occlusion = read_member(archive, "_AmbientOcclusion.jpg")
        colour = read_member(archive, "_Color.jpg")
        if roughness is None:
            # A surface-imperfection set (smudges, scratches) is one grey mask: it is the roughness variation, and
            # it leaves the colour alone.
            roughness, colour = read_member(archive, "_Opacity.jpg") or colour, None
        if normal is None or roughness is None:
            raise RuntimeError("%s has no normal or roughness map" % name)
        size = normal.size

        def channel(image):
            return np.asarray(image.convert("L").resize(size, Image.LANCZOS), dtype=np.float32) / 255.0

        rough = channel(roughness)
        rough = np.clip(rough - rough.mean() + 0.5, 0.0, 1.0)
        ao = channel(occlusion) if occlusion is not None else np.ones_like(rough)
        if colour is not None:
            luma = channel(colour)
            luma = np.clip(luma / max(float(luma.mean()), 1e-3) * 0.5, 0.0, 1.0)
        else:
            luma = np.full_like(rough, 0.5)
        packed = np.stack([ao, rough, luma], axis=-1)
        Image.fromarray((packed * 255.0 + 0.5).astype(np.uint8), "RGB").save(packed_path, quality=93, subsampling=0)
        normal.convert("RGB").save(normal_path, quality=95, subsampling=0)


def measure_set(name, out):
    """How much a packed set varies. Sets differ wildly (a clean metal's roughness barely moves, gravel's normal
    swings hard), so the importer scales each one to a common strength from these numbers; without that a
    surface class's "how much of the set shows" would mean something different for every set."""
    packed = np.asarray(Image.open(os.path.join(out, "T_%s_ORD.jpg" % name)).resize((512, 512)), dtype=np.float32) / 255.0
    normal = np.asarray(Image.open(os.path.join(out, "T_%s_N.jpg" % name)).resize((512, 512)), dtype=np.float32) / 127.5 - 1.0
    return {
        "normal_rms": round(float(np.sqrt((normal[..., :2] ** 2).sum(axis=-1).mean())), 3),
        "rough_std": round(float(packed[..., 1].std()), 3),
        "luma_std": round(float(packed[..., 2].std()), 3),
    }


def read_hdr(path):
    """A Radiance .hdr (run-length encoded RGBE) as float32 RGB."""
    with open(path, "rb") as f:
        data = f.read()
    end = data.index(b"\n\n") + 2
    line_end = data.index(b"\n", end)
    parts = data[end:line_end].split()
    height, width = int(parts[1]), int(parts[3])
    position = line_end + 1
    rgbe = np.zeros((height, width, 4), dtype=np.uint8)
    if len(data) - position == width * height * 4:
        # Not run-length encoded (as write_hdr below writes them).
        rgbe = np.frombuffer(data, dtype=np.uint8, offset=position).reshape(height, width, 4)
        height = 0
    for y in range(height):
        if data[position] != 2 or data[position + 1] != 2:
            raise RuntimeError("unsupported .hdr encoding in " + path)
        position += 4
        for c in range(4):
            x = 0
            while x < width:
                count = data[position]
                position += 1
                if count > 128:
                    count -= 128
                    rgbe[y, x:x + count, c] = data[position]
                    position += 1
                else:
                    rgbe[y, x:x + count, c] = np.frombuffer(data, dtype=np.uint8, count=count, offset=position)
                    position += count
                x += count
    scale = np.ldexp(1.0, rgbe[..., 3].astype(np.int32) - 136).astype(np.float32)
    scale[rgbe[..., 3] == 0] = 0.0
    return rgbe[..., :3].astype(np.float32) * scale[..., None]


def write_hdr(path, rgb):
    """Float RGB to an uncompressed Radiance .hdr."""
    brightest = np.maximum(rgb.max(axis=-1), 1e-32)
    exponent = np.ceil(np.log2(brightest)).astype(np.int32)
    mantissa = rgb / np.ldexp(1.0, exponent)[..., None]
    rgbe = np.zeros(rgb.shape[:2] + (4,), dtype=np.uint8)
    rgbe[..., :3] = np.clip(mantissa * 256.0, 0, 255).astype(np.uint8)
    rgbe[..., 3] = np.clip(exponent + 128, 0, 255).astype(np.uint8)
    rgbe[brightest <= 1e-32] = 0
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (rgb.shape[0], rgb.shape[1]))
        f.write(rgbe.tobytes())


def small_copy(source, target, width=1024):
    """A box-filtered small copy: sky lighting needs no detail, and a small cube map filters cleanly."""
    if os.path.exists(target):
        return
    rgb = read_hdr(source)
    factor = rgb.shape[1] // width
    height = rgb.shape[0] // factor
    small = rgb[:height * factor, :width * factor].reshape(height, factor, width, factor, 3).mean(axis=(1, 3))
    # The moon's disc is thousands of times brighter than the sky around it. Left in, the sky light would throw
    # its light a second time, as noise; the level's moon light does that job with real shadows.
    luminance = small @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    ceiling = 12.0 * float(np.median(luminance[:height // 2]))
    small *= np.minimum(1.0, ceiling / np.maximum(luminance, 1e-6))[..., None]
    write_hdr(target, small.astype(np.float32))


def water_normal(target, size=512):
    """A tiling normal map of small waves, made here (no download): the water material pans two copies of it."""
    if os.path.exists(target):
        return
    random = np.random.default_rng(7)
    frequency = np.fft.fftfreq(size) * size
    fx, fy = np.meshgrid(frequency, frequency)
    distance = np.sqrt(fx * fx + fy * fy)
    # Energy in a band of wavelengths (size/24 to size/4 pixels) gives ripples rather than white noise.
    band = np.exp(-((distance - 9.0) / 7.0) ** 2) + 0.35 * np.exp(-((distance - 30.0) / 14.0) ** 2)
    spectrum = band * np.exp(2j * np.pi * random.random((size, size)))
    height = np.real(np.fft.ifft2(spectrum))
    height /= np.abs(height).max()
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 6.0
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 6.0
    normal = np.stack([-dx, dy, np.ones_like(dx)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    Image.fromarray(np.clip((normal * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB").save(target)


def main():
    downloads, out = sys.argv[1], sys.argv[2]
    catalogue_path = os.path.join(HERE, "pbr_sets.json")
    catalogue = json.load(open(catalogue_path))
    os.makedirs(out, exist_ok=True)
    measured = False
    for name, record in catalogue["sets"].items():
        zip_path = os.path.join(downloads, name, name + ".zip")
        download(record["url"], zip_path)
        pack_set(name, zip_path, out)
        stats = measure_set(name, out)
        if record.get("stats") != stats:
            record["stats"], measured = stats, True
        print("set ready: " + name)
    if measured:
        # The measurements are committed with the catalogue: the map import needs them and has only the uassets.
        with open(catalogue_path, "w", newline="\n") as f:
            json.dump(catalogue, f, indent=2)
            f.write("\n")
    for name, record in catalogue["hdris"].items():
        hdr_path = os.path.join(downloads, name, name + ".hdr")
        download(record["url"], hdr_path)
        sky = os.path.join(out, name + ".hdr")
        if not os.path.exists(sky):
            with open(hdr_path, "rb") as source, open(sky, "wb") as target:
                target.write(source.read())
        small_copy(hdr_path, os.path.join(out, name + "_light.hdr"))
        print("sky ready: " + name)
    water_normal(os.path.join(out, "T_WaterNormal.png"))


if __name__ == "__main__":
    main()
