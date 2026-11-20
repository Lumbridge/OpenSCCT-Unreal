"""Generate the linear roughness and metallic masks for Mount Hospital's door and floor."""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def door_finish():
    size = (1024, 512)
    roughness = Image.new("L", size, 130)
    metallic = Image.new("L", size, 0)
    # Door trim, the horizontal plate and the vertical plate, in normalized atlas coordinates.
    plates = (
        ((0, 0, .214, 1), 111, 224),
        ((.46, .09, .603, .169), 88, 224),
        ((.808, .367, .869, .634), 79, 238),
    )
    for rect, roughness_value, metallic_value in plates:
        box = tuple(round(value * extent) for value, extent in zip(rect, size * 2))
        ImageDraw.Draw(roughness).rectangle(box, fill=roughness_value)
        ImageDraw.Draw(metallic).rectangle(box, fill=metallic_value)
    feather = ImageFilter.GaussianBlur(1.2)
    return Image.merge("RGB", (roughness.filter(feather), metallic.filter(feather), Image.new("L", size)))


def floor_finish():
    y, x = np.indices((512, 512))
    tile = 256
    joint_x = np.minimum(x % tile, tile - x % tile)
    joint_y = np.minimum(y % tile, tile - y % tile)
    joint = np.minimum(joint_x, joint_y)
    roughness = .31 + .40 * np.exp(-(joint / 1.8) ** 2) + .075 * np.exp(-joint / 9)
    channels = np.zeros((512, 512, 3), dtype=np.uint8)
    channels[:, :, 0] = np.rint(255 * roughness).astype(np.uint8)
    return Image.fromarray(channels)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "Art/Maps/MounE")
    directory = parser.parse_args().output
    directory.mkdir(parents=True, exist_ok=True)
    door_finish().save(directory / "T_HospitalDoorFinish.png", optimize=True)
    floor_finish().save(directory / "T_HospitalFloorFinish.png", optimize=True)
