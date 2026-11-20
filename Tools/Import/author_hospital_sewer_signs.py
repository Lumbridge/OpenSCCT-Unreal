"""Redraw the two sewer signs and cast-iron grille in their existing atlas rectangles."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont


DIRECTION_ATLAS = "Prison_TEX_pan_hospital.png"
DANGER_ATLAS = "Prison_TEX_pan_danger.png"
END_ATLASES = (("Prison_TEX_pan_end.png", "Rooms 200 - 217", False),
              ("Prison_TEX_pan_end1.png", "Rooms 100 - 117", True))
SOUTH = ((141, 65, 241, 104), (235, 209, 143), 420,
         (("S", (146.5, 68.6, 161.0, 100.9)), ("O", (163.7, 68.5, 181.6, 101.0)),
          ("U", (184.8, 69.4, 200.3, 101.0)), ("T", (202.0, 69.2, 217.8, 100.6)),
          ("H", (219.7, 69.2, 235.6, 100.7))))
MAIN = ((28, 192, 237, 251), (227, 223, 201), 520,
        (("M", (33.0, 195.1, 63.2, 247.9)), ("A", (66.4, 195.1, 95.6, 248.0)),
         ("I", (99.6, 195.0, 107.4, 248.0)), ("N", (112.8, 195.1, 138.4, 248.0)),
         ("S", (169.8, 194.6, 194.9, 248.8)), ("T", (197.5, 195.2, 221.9, 248.0)),
         (".", (228.7, 241.4, 234.1, 248.1))))
OTHER_DIRECTIONS = (
    ((29, 11, 104, 49), (227, 227, 202), 430, (("EAST", (33.0, 13.6, 100.1, 45.3)),)),
    ((151, 11, 235, 49), (240, 218, 149), 430, (("WEST", (155.6, 13.8, 231.7, 45.9)),)),
    ((15, 65, 111, 104), (226, 225, 207), 430, (("NORTH", (18.0, 69.0, 107.5, 100.7)),)),
    ((39, 130, 222, 237), (232, 230, 203), 450, (("NSP", (95.0, 133.5, 164.4, 176.7)),
                                                ("HOSPITAL", (43.1, 191.1, 217.0, 234.3)))),
)
DEAD = ((29, 18, 226, 98), (33, 27, 19), 405,
        tuple(zip("DEADEND", ((34.5, 23.4, 55.1, 93.0), (59.9, 23.4, 77.6, 93.0),
                             (80.0, 23.1, 103.8, 93.0), (107.0, 23.4, 126.8, 93.0),
                             (152.8, 23.4, 171.1, 93.0), (175.7, 23.4, 195.2, 93.0),
                             (201.3, 23.4, 221.8, 93.0)))))
DEAD_ARROW = ((70.3, 123.3), (161.1, 123.3), (150.5, 110.2), (183.5, 110.2),
              (213.8, 139.9), (184.8, 169.9), (149.4, 169.9), (160.1, 158.0), (70.3, 158.0))
DEAD_ARROW_TAIL = ((37.3, 123.3), (59.8, 123.3), (59.8, 158.2), (37.3, 158.2))
ROOM_ARROW = ((68.5, 245.5), (84.1, 239.5), (84.1, 242.8), (191.0, 242.8),
              (191.0, 247.6), (84.1, 247.6), (84.1, 251.3))
DANGER = ((31, 32, 221, 78), (224, 214, 179), 550, (("DANGER", (36.8, 35.0, 216.4, 75.0)),))
GRILLE = "Prison_TEX_trouegouts.png"
GRILLE_BARS = (27.6, 35.15, 44.8, 53.4, 62.9, 71.9, 81.0, 89.0, 96.7, 104.0)


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def field(shape, spacing, seed):
    h, w = shape
    noise = np.random.default_rng(seed).standard_normal((max(2, h // spacing), max(2, w // spacing))).astype(np.float32)
    return np.asarray(Image.fromarray(noise).resize((w, h), Image.Resampling.BICUBIC))


def read_inputs(name, folder, hd, min_scale=4):
    stock = Image.open(folder / name).convert("RGB")
    current = Image.open(hd / name).convert("RGBA")
    scale, remainder = divmod(current.width, stock.width)
    if remainder or scale < 4 or current.height != stock.height * scale:
        raise ValueError(f"{name}: expected at least a 4x authored atlas")
    if scale < min_scale:
        current = current.resize((stock.width * min_scale, stock.height * min_scale), Image.Resampling.LANCZOS)
        scale = min_scale
    return np.asarray(stock), current, scale


def glyph_image(text, bounds, size, font, scale):
    xy0 = tuple(round(coord * scale) for coord in bounds[:2])
    xy1 = tuple(round(coord * scale) for coord in bounds[2:])
    left, top, right, bottom = font.getbbox(text)
    letter = Image.new("L", (right - left + 8, bottom - top + 8))
    ImageDraw.Draw(letter).text((4 - left, 4 - top), text, font=font, fill=255)
    ink = letter.getbbox()
    if ink is None:
        raise ValueError("Missing glyph: " + text)
    glyph = letter.crop(ink).resize((xy1[0] - xy0[0], xy1[1] - xy0[1]), Image.Resampling.LANCZOS)
    result = Image.new("L", size)
    result.paste(glyph, xy0)
    return result


def paint_panel(data, stock, current, scale, font_path, ink_type="cream", polygons=()):
    region, tint, weight, shapes = data
    west, north, east, south = region
    crop = stock[north:south, west:east].astype(np.float32)
    red, green, blue = crop.transpose(2, 0, 1)
    if ink_type == "black":
        old_ink = (red < 126) & (green < 118)
    elif ink_type == "paper":
        old_ink = (red < 190) & (green < 190) & (blue < 180)
    elif ink_type == "cream_on_red":
        old_ink = (green > 92) & (blue > 63) & (green > .49 * red)
    else:
        old_ink = (red > 37) & ((red > .65 * green) | (blue > .77 * green))
    old_ink = old_ink.astype(np.uint8) * 255
    old_ink = cv2.dilate(old_ink, np.ones((3, 3), np.uint8), iterations=1)
    traced = cv2.inpaint(crop.astype(np.uint8), old_ink, 3.0, cv2.INPAINT_TELEA)
    rect = tuple(v * scale for v in region)
    existing = np.asarray(current.crop(rect)).copy()
    size = (existing.shape[1], existing.shape[0])
    restored = np.asarray(Image.fromarray(traced).resize(size, Image.Resampling.LANCZOS)).astype(np.float32)
    cover = np.asarray(Image.fromarray(old_ink).resize(size, Image.Resampling.BILINEAR), dtype=np.float32) / 255.0
    canvas = existing[..., :3] * (1 - cover[..., None]) + restored * cover[..., None]

    font = ImageFont.truetype(str(font_path), 440)
    font.set_variation_by_axes([weight, 82])
    ink = np.zeros(size[::-1], dtype=np.float32)
    for text, (x0, y0, x1, y1) in shapes:
        bounds = (x0 - west, y0 - north, x1 - west, y1 - north)
        ink = np.maximum(ink, np.asarray(glyph_image(text, bounds, size, font, scale)) / 255.0)
    if polygons:
        shape = Image.new("L", (size[0] * 2, size[1] * 2))
        draw = ImageDraw.Draw(shape)
        for polygon in polygons:
            draw.polygon([((x - west) * scale * 2, (y - north) * scale * 2) for x, y in polygon], fill=255)
        ink = np.maximum(ink, np.asarray(shape.resize(size, Image.Resampling.LANCZOS)) / 255.0)

    # Keep the sign's existing dark patina. Small flecks chip the paint without obscuring the lettering.
    local_green = restored[..., 1]
    aged = np.clip((np.median(local_green) - local_green) * .23, -12, 25)
    grain = field(ink.shape, 4, 144) * 3.1 + field(ink.shape, 15, 51) * 5.5
    grain += field(ink.shape, 28, 164) * 4.5
    chip_field = field(ink.shape, 5, 122) + .48 * field(ink.shape, 14, 115)
    flecks = smooth(1.00, 2.65, chip_field) * smooth(-11, 32, aged + 19) * .59
    paint = np.array(tint) + (grain - aged * 1.36)[..., None]
    alpha = (ink * (1 - flecks))[..., None]
    existing[..., :3] = np.clip(canvas * (1 - alpha) + paint * alpha, 0, 255).round().astype(np.uint8)
    current.paste(Image.fromarray(existing), rect[:2])


def redraw_signs(source, hd, output, font):
    stock, painted, scale = read_inputs(DIRECTION_ATLAS, source, hd, 8)
    for panel in (*OTHER_DIRECTIONS, SOUTH):
        paint_panel(panel, stock, painted, scale, font)
    painted.save(output / DIRECTION_ATLAS)

    stock, painted, scale = read_inputs(DANGER_ATLAS, source, hd, 8)
    paint_panel(MAIN, stock, painted, scale, font)
    paint_panel(DANGER, stock, painted, scale, font, "cream_on_red")
    painted.save(output / DANGER_ATLAS)

    for name, rooms, mirror in END_ATLASES:
        stock, painted, scale = read_inputs(name, source, hd, 8)
        paint_panel(DEAD, stock, painted, scale, font, "black")
        arrows = (DEAD_ARROW, DEAD_ARROW_TAIL)
        if mirror:
            arrows = tuple(tuple((254.0 - x, y) for x, y in points) for points in arrows)
        paint_panel(((29, 106, 221, 174), (34, 28, 21), 425, ()),
                    stock, painted, scale, font, "black", arrows)
        room_panel = ((13, 202, 243, 253), (55, 55, 49), 630, ((rooms, (17.2, 208, 238.2, 235.8)),))
        paint_panel(room_panel, stock, painted, scale, font, "paper", (ROOM_ARROW,))
        painted.save(output / name)


def grille_geometry(width, height, scale):
    y, x = np.ogrid[:height, :width]
    x = (x + .5) / scale
    y = (y + .5) / scale
    radial = np.hypot(x - 192.8, (y - 64.0) * 1.01)
    bar_y = np.array(GRILLE_BARS)[np.abs(y - np.array(GRILLE_BARS)).argmin(axis=1), None]
    return radial, y - bar_y


def redraw_grille(source, hd, output):
    stock, current, scale = read_inputs(GRILLE, source, hd)
    height, width = stock.shape[:2]
    radius, bars = grille_geometry(width, height, 1)
    old_edges = ((np.abs(radius - 45.3) < 2.8) | ((radius < 44.8) & (np.abs(bars) < 1.5))).astype(np.uint8) * 255
    patina = cv2.inpaint(stock, old_edges, 3.0, cv2.INPAINT_TELEA)
    clean = np.asarray(Image.fromarray(patina).resize(current.size, Image.Resampling.LANCZOS)).astype(np.float32)
    cover = np.asarray(Image.fromarray(old_edges).resize(current.size, Image.Resampling.BILINEAR), dtype=np.float32) / 255
    pixels = np.asarray(current).copy()
    bg = pixels[..., :3] * (1 - cover[..., None]) + clean * cover[..., None]
    radial, bar = grille_geometry(current.width, current.height, scale)
    inside = 1 - smooth(44.0, 44.4, radial)
    steel = (1 - smooth(.56, .85, np.abs(bar))) * inside
    ring = 1 - smooth(.52, .76, np.abs(radial - 45.3))
    under = smooth(.63, 1.12, bar) * (1 - smooth(1.12, 1.75, bar)) * inside
    bg *= (1 - .32 * under[..., None])
    edge_light = 26 * np.exp(-((bar + .27) / .26) ** 2) - 34 * smooth(.2, .9, bar)
    stain = field(bg.shape[:2], 20, 233) * 5.6 + field(bg.shape[:2], 5, 172) * 2.7
    bar_color = np.array((117, 103, 82)) + (edge_light + stain)[..., None]
    ring_color = np.array((125, 109, 85)) + (17 * np.cos((radial - 45.25) * 2.5) + stain)[..., None]
    bg = bg * (1 - steel[..., None]) + bar_color * steel[..., None]
    bg = bg * (1 - ring[..., None]) + ring_color * ring[..., None]
    pixels[..., :3] = np.clip(bg, 0, 255).round().astype(np.uint8)
    Image.fromarray(pixels).save(output / GRILLE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("hd", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--font", type=Path, default=Path("C:/Windows/Fonts/bahnschrift.ttf"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    redraw_signs(args.source, args.hd, args.output, args.font)
    redraw_grille(args.source, args.hd, args.output)
    names = [DIRECTION_ATLAS, DANGER_ATLAS, *(row[0] for row in END_ATLASES), GRILLE]
    (args.output / "manifest.json").write_text(json.dumps({name: name for name in names}, indent=2) + "\n", encoding="utf-8")
    for name in names:
        print(f"{name}: {Image.open(args.output / name).size}")


if __name__ == "__main__":
    main()
