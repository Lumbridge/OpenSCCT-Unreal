"""Paints the merc rifle's textures. Plain Python with numpy, scipy and Pillow:

    python paint_textures.py <work folder>

merc_rifle.py (stage "bake") leaves, for every texel of the rifle's texture atlas, what surface it is, where it
is on the rifle, which way it faces, how occluded it is and how close to an edge. Everything here is painted in
the rifle's own space from those: noise that runs unbroken over UV seams, wear on the outward edges, dust in the
creases, grooves and lettering placed by their position on the gun. The result is the PBR set the game uses:

    textures/T_MercRifle_BaseColor.png   sRGB colour
    textures/T_MercRifle_Normal.png      tangent space, +Y up (OpenGL); Unreal flips green on import
    textures/T_MercRifle_ORM.png         R ambient occlusion, G roughness, B metallic
    textures/T_MercRifle_Emissive.png    sRGB colour of what glows: reticle, screen, lenses, status light

All positions are in the design frame of merc_rifle.py: millimetres, X along the barrel from the trigger, Z up
from the bore, +Y the shooter's left.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, "..", "..", "..", "Saved", "MercRifle"))
OUT = os.path.join(HERE, "textures")
# Lettering is set in Inter (SIL Open Font License), the copy that ships with Blender.
FONT = sys.argv[2] if len(sys.argv) > 2 else "C:/Program Files/Blender Foundation/Blender 5.2/5.2/datafiles/fonts/Inter.woff2"

RIG = json.load(open(os.path.join(HERE, "rifle_rig.json"), encoding="utf-8"))
SURFACE = RIG["surfaces"]
ORIGIN = np.array(RIG["origin_mm"], dtype=np.float32)


def log(text):
    print("MERC-RIFLE paint: " + text, flush=True)


# ---------------------------------------------------------------------------------------------------------------
# The baked buffers, as flat lists over the used texels
# ---------------------------------------------------------------------------------------------------------------

ids = np.load(os.path.join(WORK, "ids.npy"))
SIZE = ids.shape[0]
VALID = ids[:, :, 2] > 0.5
ROWS, COLS = np.nonzero(VALID)
KIND = np.rint(ids[ROWS, COLS, 0] * 32.0).astype(np.int32)
del ids
POSITION = np.load(os.path.join(WORK, "position.npy")) - 1.0             # metres, model space, kept for the normals
P = POSITION[ROWS, COLS] * 1000.0 + ORIGIN                               # millimetres, design frame
N = np.load(os.path.join(WORK, "normal.npy"))[ROWS, COLS] * 2.0 - 1.0
N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-6)
AO = np.clip(np.load(os.path.join(WORK, "occlusion.npy"))[ROWS, COLS, 0], 0.0, 1.0)
_edges = np.load(os.path.join(WORK, "edges.npy"))[ROWS, COLS]
EDGE, POINT = _edges[:, 0], _edges[:, 1]
del _edges
COUNT = len(KIND)
log("%d texels in use (%.0f%% of the atlas)" % (COUNT, 100.0 * COUNT / SIZE / SIZE))


def is_kind(*names):
    return np.isin(KIND, [SURFACE[name] for name in names])


def smoothstep(low, high, value):
    t = np.clip((value - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def to_linear(srgb):
    srgb = np.asarray(srgb, dtype=np.float32)
    return np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def to_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1.0 / 2.4) - 0.055)


# ---------------------------------------------------------------------------------------------------------------
# Noise in the rifle's space
# ---------------------------------------------------------------------------------------------------------------

def _hash(ix, iy, iz, seed):
    h = (ix * 374761393 + iy * 668265263 + iz * 1274126177 + seed * 974711) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1103515245) & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return (h & 0xFFFFFF).astype(np.float32) / 16777216.0


def noise(points, scale, seed=0):
    """Smooth value noise, 0..1. scale is the size of a feature in millimetres (one number or one per axis)."""
    p = points / np.asarray(scale, dtype=np.float32)
    cell = np.floor(p)
    f = p - cell
    f = f * f * (3.0 - 2.0 * f)
    ix, iy, iz = (cell[:, i].astype(np.int64) for i in range(3))
    result = np.zeros(len(points), dtype=np.float32)
    for dx in (0, 1):
        wx = f[:, 0] if dx else 1.0 - f[:, 0]
        for dy in (0, 1):
            wy = f[:, 1] if dy else 1.0 - f[:, 1]
            for dz in (0, 1):
                wz = f[:, 2] if dz else 1.0 - f[:, 2]
                result += _hash(ix + dx, iy + dy, iz + dz, seed) * wx * wy * wz
    return result


def fbm(points, scale, octaves=3, seed=0):
    total = np.zeros(len(points), dtype=np.float32)
    weight, norm = 1.0, 0.0
    for octave in range(octaves):
        total += weight * noise(points, np.asarray(scale, dtype=np.float32) / (2.0 ** octave), seed + octave * 17)
        norm += weight
        weight *= 0.5
    return total / norm


# ---------------------------------------------------------------------------------------------------------------
# Base materials
# ---------------------------------------------------------------------------------------------------------------

# surface: (linear base colour, roughness, metallic, edge wear amount)
LOOKS = {
    "metal":        ((0.095, 0.099, 0.110), 0.41, 1.0, 1.0),
    "polymer":      ((0.021, 0.022, 0.023), 0.56, 0.0, 0.5),
    "polymer_grey": ((0.078, 0.083, 0.082), 0.62, 0.0, 0.5),
    "grip":         ((0.015, 0.015, 0.016), 0.76, 0.0, 0.25),
    "rubber":       ((0.011, 0.011, 0.011), 0.86, 0.0, 0.12),
    "steel_dark":   ((0.085, 0.086, 0.092), 0.36, 1.0, 0.9),
    "steel_bright": ((0.640, 0.620, 0.580), 0.24, 1.0, 0.0),
    "suppressor":   ((0.170, 0.125, 0.085), 0.50, 1.0, 0.8),
    "brass":        ((0.900, 0.690, 0.350), 0.27, 1.0, 0.0),
    "copper":       ((0.830, 0.460, 0.290), 0.30, 1.0, 0.0),
    "accent":       ((0.780, 0.170, 0.012), 0.46, 0.0, 0.5),
    "cavity":       ((0.006, 0.006, 0.007), 0.80, 0.0, 0.0),
    "torch_lens":   ((0.020, 0.020, 0.020), 0.06, 0.0, 0.0),
    "laser_lens":   ((0.050, 0.004, 0.004), 0.06, 0.0, 0.0),
    "screen":       ((0.004, 0.006, 0.008), 0.10, 0.0, 0.0),
    "led":          ((0.020, 0.100, 0.040), 0.30, 0.0, 0.0),
    "dark_glass":   ((0.010, 0.004, 0.018), 0.05, 0.0, 0.0),
    "glass":        ((0.030, 0.050, 0.058), 0.04, 0.0, 0.0),   # a faint cool tint: glass is mostly its reflections
}
# What shows where the finish has worn off an edge: (colour, roughness, metallic), or None to lighten the base.
WORN = {
    "metal": ((0.78, 0.78, 0.80), 0.30, 1.0),
    "steel_dark": ((0.52, 0.52, 0.54), 0.30, 1.0),
    "suppressor": ((0.60, 0.57, 0.53), 0.32, 1.0),
}

albedo = np.zeros((COUNT, 3), dtype=np.float32)
rough = np.zeros(COUNT, dtype=np.float32)
metal = np.zeros(COUNT, dtype=np.float32)
wear_amount = np.zeros(COUNT, dtype=np.float32)
height = np.zeros(COUNT, dtype=np.float32)      # millimetres, out of the surface
emissive = np.zeros((COUNT, 3), dtype=np.float32)
for name, (colour, roughness, metallic, wear) in LOOKS.items():
    chosen = KIND == SURFACE[name]
    albedo[chosen], rough[chosen], metal[chosen], wear_amount[chosen] = colour, roughness, metallic, wear

log("noise")
big = fbm(P, 70.0, 3, seed=1)          # slow drift in tone
medium = fbm(P, 9.0, 3, seed=2)        # blotches: handling marks, uneven finish
fine = fbm(P, 1.4, 2, seed=3)          # grain

# Uneven finish: nothing on a used rifle is one flat colour or one flat gloss.
albedo *= (1.0 + (big - 0.5) * 0.30 + (medium - 0.5) * 0.14)[:, None]
# Gloss varies less on metal, where every change shows as a dark or bright cloud in the reflection.
rough += ((big - 0.5) * 0.10 + (medium - 0.5) * 0.12) * lerp(1.0, 0.45, metal) + (fine - 0.5) * 0.05

# Greasy handling marks on the metal: patches a little glossier.
smear = smoothstep(0.55, 0.75, fbm(P, 22.0, 3, seed=5)) * is_kind("metal", "steel_dark", "suppressor")
rough -= smear * 0.04

# The magazine windows are smoked, the sight lens a cool coated glass.
windows = is_kind("glass") & (P[:, 2] < -40.0)
albedo[windows] = (0.060, 0.048, 0.034)

# The suppressor has been run hot: the finish goes darker and bluer towards the muzzle.
shroud = is_kind("suppressor")
heat = smoothstep(300.0, 396.0, P[:, 0]) * shroud
albedo = lerp(albedo, albedo * np.array([0.55, 0.62, 0.95], dtype=np.float32), (heat * 0.75)[:, None])
rough += heat * 0.05

# ---------------------------------------------------------------------------------------------------------------
# Surface relief
# ---------------------------------------------------------------------------------------------------------------

log("relief")
# Moulded polymer has a fine pebbled grain; anodised metal is nearly smooth.
grain = (noise(P, 0.55, seed=11) - 0.5) * 2.0
height += grain * 0.022 * is_kind("polymer", "polymer_grey")
height += grain * 0.004 * is_kind("metal", "steel_dark")
height += grain * 0.012 * is_kind("rubber", "accent")


def crosshatch(a, b, pitch):
    """A field of diamond studs over two in-plane coordinates."""
    return np.abs(np.sin(math.pi * (a + b) / pitch) * np.sin(math.pi * (a - b) / pitch))


# Grip surfaces: diamond stippling, laid out in whichever plane the face lies in.
grip = is_kind("grip")
side_on = np.abs(N[:, 1]) > 0.6
front_on = (np.abs(N[:, 0]) >= np.abs(N[:, 2])) & ~side_on
studs = np.where(side_on, crosshatch(P[:, 0], P[:, 2], 2.4),
                 np.where(front_on, crosshatch(P[:, 1], P[:, 2], 2.4), crosshatch(P[:, 0], P[:, 1], 2.4)))
# The pistol grip keeps a smooth border, as a moulded panel would.
border = np.ones(COUNT, dtype=np.float32)
on_pistol_grip = grip & (P[:, 0] < 60.0)
border[on_pistol_grip] = smoothstep(-152.0, -146.0, P[on_pistol_grip, 2]) * smoothstep(-44.0, -52.0, P[on_pistol_grip, 2])
height += studs * 0.30 * grip * border
rough += studs * 0.06 * grip

# Butt pad: horizontal ribs on the face that meets the shoulder.
pad = is_kind("rubber") & (P[:, 0] < -340.0) & (N[:, 0] < -0.5)
height += (np.abs(np.sin(math.pi * P[:, 2] / 6.0)) ** 0.6) * 0.5 * pad

# Knurling round the charging handle knob and the sight's clamp knob.
for cx, cz in ((195.0, 13.5), (-6.0, 49.5)):
    knob = is_kind("steel_dark") & (np.hypot(P[:, 0] - cx, P[:, 2] - cz) < 6.5) & (P[:, 1] > 19.0) & (np.abs(N[:, 1]) < 0.5)
    angle = np.arctan2(P[:, 2] - cz, P[:, 0] - cx) * 6.0
    height += crosshatch(angle, P[:, 1], 1.5) * 0.18 * knob

# The shroud is turned: faint tool rings along its length. The bolt carrier is brushed lengthwise.
height += np.sin(2.0 * math.pi * P[:, 0] / 0.9) * 0.004 * shroud
rough += (noise(P, (0.5, 60.0, 60.0), seed=21) - 0.5) * 0.08 * shroud
brushed = is_kind("steel_bright")
streak = noise(P, (40.0, 0.35, 0.35), seed=22) - 0.5
rough += streak * 0.16 * brushed
albedo *= (1.0 + streak * 0.2 * brushed)[:, None]


# ---------------------------------------------------------------------------------------------------------------
# Marks placed on the rifle: grooves, screws, lettering
# ---------------------------------------------------------------------------------------------------------------

def groove(points, surfaces, side=None, width=0.45, depth=0.28, near=(-1e9, 1e9)):
    """A panel line: a polyline drawn on the rifle's side (X, Z), pressed into the given surfaces. side +1 is the
    left, -1 the right, None both. near limits it to faces whose |Y| lies in that range."""
    global height, albedo, rough
    chosen = is_kind(*surfaces) & (np.abs(N[:, 1]) > 0.7) & (np.abs(P[:, 1]) >= near[0]) & (np.abs(P[:, 1]) <= near[1])
    if side is not None:
        chosen &= (P[:, 1] * side) > 0
    index = np.nonzero(chosen)[0]
    px, pz = P[index, 0], P[index, 2]
    distance = np.full(len(index), 1e9, dtype=np.float32)
    for (ax, az), (bx, bz) in zip(points, points[1:]):
        dx, dz = bx - ax, bz - az
        t = np.clip(((px - ax) * dx + (pz - az) * dz) / (dx * dx + dz * dz), 0.0, 1.0)
        distance = np.minimum(distance, np.hypot(px - (ax + t * dx), pz - (az + t * dz)))
    cut = 1.0 - smoothstep(width * 0.5, width * 1.2, distance)
    height[index] -= cut * depth
    albedo[index] *= (1.0 - cut * 0.6)[:, None]
    rough[index] = lerp(rough[index], 0.8, cut * 0.6)


log("grooves and screws")
LOWER, UPPER, GUARD = (22.5, 24.5), (20.0, 21.6), (23.5, 25.5)
groove([(-150, -36), (-100, -36), (-92, -28), (-58, -28)], ["polymer"], near=LOWER)
groove([(-12, -15.5), (-12, -36), (58, -36), (68, -27), (68, -15.5)], ["polymer"], near=LOWER)
groove([(-336, -31), (-262, -31)], ["polymer"], near=LOWER)
groove([(-242, -15.5), (-242, -58)], ["polymer"], near=LOWER)
groove([(-170, -15.5), (-170, -56)], ["polymer"], near=LOWER)
groove([(-160, -12), (-160, 28)], ["metal"], near=UPPER)
groove([(64, 28), (64, 18), (84, 18)], ["metal"], near=UPPER)
groove([(-244, -8), (-164, -8)], ["metal"], side=-1, near=UPPER)
groove([(92, -9), (226, -9)], ["polymer"], near=GUARD)
groove([(136, -9), (136, -29)], ["polymer"], near=GUARD)
groove([(180, -40), (180, -70)], ["metal"], near=(16.0, 18.0))
groove([(-232, -72), (-180, -72)], ["polymer"], near=(12.0, 14.0))

# Every screw head gets a hex socket.
for item in RIG["screws"]:
    centre = np.array(item["position"], dtype=np.float32) * 1000.0 + ORIGIN
    radius = item["radius"] * 1000.0
    side = item["normal"][1]
    index = np.nonzero(is_kind("steel_dark") & (N[:, 1] * side > 0.7) & (np.abs(P[:, 1] - centre[1]) < 0.6)
                       & (np.hypot(P[:, 0] - centre[0], P[:, 2] - centre[2]) < radius))[0]
    dx, dz = np.abs(P[index, 0] - centre[0]), np.abs(P[index, 2] - centre[2])
    socket = 1.0 - smoothstep(radius * 0.50, radius * 0.60, np.maximum(dx, dx * 0.5 + dz * 0.866))
    height[index] -= socket * 0.9
    albedo[index] *= (1.0 - socket * 0.75)[:, None]
    rough[index] = lerp(rough[index], 0.7, socket)


class Canvas:
    """A small drawing in millimetres (text, lines, shapes), to be stamped onto the rifle."""

    def __init__(self, width, height_mm, density=24):
        self.width, self.height, self.density = width, height_mm, density
        self.image = Image.new("RGBA", (int(width * density), int(height_mm * density)), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.image)

    def _px(self, points):
        return [(x * self.density, y * self.density) for x, y in points]

    def text(self, x, y, size, text, colour=(255, 255, 255, 255), anchor="lm", spacing=0.0):
        """Text with capitals 'size' millimetres tall; (x, y) from the top left, placed by its anchor."""
        font = ImageFont.truetype(FONT, int(size * self.density / 0.727))   # Inter's capitals are 0.727 em tall
        font.set_variation_by_name("Bold")
        # Drawn letter by letter (for the tracking) on its own layer, then squeezed: the narrow, even lettering
        # of stamped and stencilled markings.
        widths = [font.getlength(letter) + spacing * self.density for letter in text]
        ascent, descent = font.getmetrics()
        layer = Image.new("RGBA", (int(sum(widths)) + 4, ascent + descent + 4), (0, 0, 0, 0))
        pen = ImageDraw.Draw(layer)
        cursor = 2.0
        for letter, width in zip(text, widths):
            pen.text((cursor, 2), letter, font=font, fill=colour)
            cursor += width
        layer = layer.resize((max(1, int(layer.width * 0.84)), layer.height), Image.LANCZOS)
        left = x * self.density - (layer.width / 2 if anchor[0] == "m" else layer.width if anchor[0] == "r" else 0)
        top = y * self.density - (2 + ascent - size * self.density / 2)     # (x, y) is the middle of the capitals
        sheet = Image.new("RGBA", self.image.size, (0, 0, 0, 0))
        sheet.paste(layer, (int(left), int(top)))
        self.image = Image.alpha_composite(self.image, sheet)
        self.draw = ImageDraw.Draw(self.image)

    def polygon(self, points, colour=(255, 255, 255, 255)):
        self.draw.polygon(self._px(points), fill=colour)

    def line(self, points, width, colour=(255, 255, 255, 255)):
        self.draw.line(self._px(points), fill=colour, width=max(1, int(width * self.density)), joint="curve")

    def rect(self, x0, y0, x1, y1, colour=(255, 255, 255, 255), outline=None, width=0.0):
        self.draw.rectangle(self._px([(x0, y0), (x1, y1)]), fill=colour, outline=outline, width=int(width * self.density))

    def circle(self, x, y, radius, colour=None, outline=None, width=0.0):
        box = self._px([(x - radius, y - radius), (x + radius, y + radius)])
        self.draw.ellipse(box, fill=colour, outline=outline, width=max(1, int(width * self.density)) if outline else 0)

    def logo(self, x, y, radius, colour=(255, 255, 255, 255)):
        """The maker's mark: a hexagon holding a double chevron."""
        corners = [(x + radius * math.cos(math.radians(60 * i + 30)), y + radius * math.sin(math.radians(60 * i + 30))) for i in range(7)]
        self.line(corners, radius * 0.16, colour)
        for drop in (-0.18, 0.22):
            self.line([(x - radius * 0.42, y + radius * (drop + 0.28)), (x, y + radius * (drop - 0.2)), (x + radius * 0.42, y + radius * (drop + 0.28))],
                      radius * 0.17, colour)

    def pixels(self):
        return np.asarray(self.image, dtype=np.float32) / 255.0


def stamp(canvas, centre, right, up, surfaces=None, depth=1.2, opacity=1.0, roughness=0.5, metallic=0.0, relief=0.0,
          glow=0.0, worn=0.0, two_sided=False, flatten=False):
    """Puts a drawing on the rifle: centred at a point, its right and up directions given in the rifle's space. It
    lands on the faces that look out of the drawing, within 'depth' millimetres of its plane.
    relief presses it in (negative) or raises it; glow writes it to the emissive map instead of the colour;
    worn (0..1) rubs patches of it away; flatten clears other relief under it first."""
    global albedo, rough, metal, height, emissive
    right, up = np.array(right, dtype=np.float32), np.array(up, dtype=np.float32)
    right /= np.linalg.norm(right)
    up /= np.linalg.norm(up)
    out = np.cross(right, up)
    chosen = np.ones(COUNT, dtype=bool) if surfaces is None else is_kind(*surfaces)
    index = np.nonzero(chosen)[0]
    rel = P[index] - np.array(centre, dtype=np.float32)
    facing = N[index] @ out
    a = rel @ right / canvas.width + 0.5
    b = rel @ up / canvas.height + 0.5
    keep = (a >= 0) & (a < 1) & (b >= 0) & (b < 1) & (np.abs(rel @ out) < depth) & ((np.abs(facing) if two_sided else facing) > 0.5)
    index, a, b, facing = index[keep], a[keep], b[keep], facing[keep]
    if two_sided:
        a = np.where(facing < 0, 1.0 - a, a)   # read the right way round from both sides
    image = canvas.pixels()
    rows = np.clip(((1.0 - b) * image.shape[0]).astype(np.int32), 0, image.shape[0] - 1)
    cols = np.clip((a * image.shape[1]).astype(np.int32), 0, image.shape[1] - 1)
    texel = image[rows, cols]
    alpha = texel[:, 3] * opacity
    if worn > 0:
        alpha *= 1.0 - worn * smoothstep(0.42, 0.62, fbm(P[index], 5.0, 3, seed=31))
    colour = to_linear(texel[:, :3])
    if glow > 0:
        emissive[index] = np.maximum(emissive[index], colour * (alpha * glow)[:, None])
        return
    albedo[index] = lerp(albedo[index], colour, alpha[:, None])
    rough[index] = lerp(rough[index], roughness, alpha)
    if metallic is not None:
        metal[index] = lerp(metal[index], metallic, alpha)
    if flatten:
        height[index] *= 1.0 - np.minimum(texel[:, 3] * 4.0, 1.0)
    height[index] += texel[:, 3] * relief


LEFT = dict(right=(-1, 0, 0), up=(0, 0, 1))    # a drawing read from the rifle's left side
RIGHT = dict(right=(1, 0, 0), up=(0, 0, 1))
ETCH = (150, 152, 156, 255)                    # laser-etched lettering: the pale metal under the finish
PAINT = (205, 203, 196, 255)                   # white paint fill, slightly aged
ORANGE = (224, 112, 26, 255)

log("lettering")
# The maker's plate on the left.
plate = Canvas(50, 19)
plate.logo(7.5, 9.5, 6.2, ETCH)
plate.text(16.5, 6.2, 4.3, "HALVARD", ETCH, spacing=0.35)
plate.text(16.7, 11.4, 1.5, "DEFENSE SYSTEMS", ETCH, spacing=0.42)
plate.line([(16.5, 13.6), (48.5, 13.6)], 0.22, ETCH)
plate.text(16.7, 16.2, 1.6, "HV-68 WARDEN    CAL 6.8\u00d743", ETCH)
stamp(plate, (-207, 22.4, 8), surfaces=["steel_dark"], roughness=0.48, metallic=1.0, relief=-0.03, **LEFT)

# Serial line and data matrix on the right of the receiver.
serial = Canvas(84, 4)
serial.text(0.5, 2, 2.3, "HALVARD HV-68 MK II  //  CAL 6.8\u00d743  //  SN 68-204417", ETCH)
stamp(serial, (-98, -21, 21.5), surfaces=["metal"], roughness=0.5, metallic=1.0, relief=-0.02, **RIGHT)
matrix = Canvas(9, 9, density=16)
cells = np.random.default_rng(68).random((12, 12)) > 0.5
cells[:, 0] = cells[-1, :] = True
for row in range(12):
    for col in range(12):
        if cells[row, col]:
            matrix.rect(col * 0.75, row * 0.75, col * 0.75 + 0.75, row * 0.75 + 0.75, ETCH)
stamp(matrix, (-152, -21, 0), surfaces=["metal"], roughness=0.5, metallic=1.0, relief=-0.02, **RIGHT)

# Fire selector positions: safe above, single, automatic below. The lever's tip points at them.
for placing, x in ((LEFT, -1.5), (RIGHT, -10.5)):
    side = 1 if placing is LEFT else -1
    for z, letter, colour in ((-19.2, "S", PAINT), (-26.0, "1", ORANGE), (-32.8, "A", ORANGE)):
        mark = Canvas(4.4, 4.4)
        mark.text(2.2, 2.2, 3.0, letter, colour, anchor="mm")
        stamp(mark, (x, side * 23.5, z), surfaces=["polymer"], roughness=0.45, relief=-0.05, **placing)

# Charging handle and vent notes on the handguard.
note = Canvas(34, 4)
note.polygon([(0.3, 2.0), (3.0, 0.6), (3.0, 3.4)], PAINT)
note.text(4.2, 2.0, 2.1, "PULL TO CHARGE", PAINT)
stamp(note, (116, 24.5, 2.5), surfaces=["polymer"], roughness=0.45, worn=0.5, **LEFT)
note = Canvas(40, 4)
note.text(20, 2.0, 2.0, "CAUTION  \u2014  HOT GAS VENTS", PAINT, anchor="mm")
stamp(note, (150, -24.5, -2.5), surfaces=["polymer"], roughness=0.45, worn=0.5, **RIGHT)

# Magazine: calibre and capacity under the window, an orange band round the floor plate.
for placing, side in ((LEFT, 1), (RIGHT, -1)):
    label = Canvas(34, 5)
    label.text(17, 2.5, 2.9, "6.8\u00d743   30", PAINT, anchor="mm")
    stamp(label, (-193, side * 13, -186), surfaces=["polymer"], roughness=0.45, worn=0.35, depth=1.0, **placing)
    band = Canvas(58, 2.6)
    band.rect(0, 0, 58, 2.6, ORANGE)
    stamp(band, (-191, side * 15.5, -197), surfaces=["polymer_grey"], roughness=0.5, worn=0.45, **placing)

# Sight and light module model names.
for placing, side in ((LEFT, 1), (RIGHT, -1)):
    label = Canvas(22, 7)
    label.text(11, 2.0, 2.5, "HX-2 HOLO", PAINT, anchor="mm")
    label.text(11, 5.4, 1.4, "HALVARD OPTICAL", PAINT, anchor="mm")
    stamp(label, (5, side * 21, 90), surfaces=["metal"], roughness=0.45, worn=0.2, **placing)
label = Canvas(30, 3)
label.text(15, 1.5, 1.9, "L3 MULTI-LIGHT", PAINT, anchor="mm")
stamp(label, (198, 17, -43.5), surfaces=["metal"], roughness=0.45, worn=0.2, **LEFT)
label = Canvas(30, 3)
label.text(15, 1.5, 1.6, "800 LM  /  VIS + IR LASER", PAINT, anchor="mm")
stamp(label, (198, 17, -66), surfaces=["metal"], roughness=0.45, worn=0.2, **LEFT)
# The laser warning label on the right: the one bright sticker on the gun.
sticker = Canvas(27, 8.5)
sticker.rect(0, 0, 27, 8.5, (226, 180, 20, 255))
sticker.rect(0.4, 0.4, 26.6, 8.1, None, outline=(12, 12, 12, 255), width=0.25)
sticker.polygon([(1.6, 6.9), (4.4, 1.6), (7.2, 6.9)], (12, 12, 12, 255))
sticker.polygon([(2.9, 6.1), (4.4, 3.3), (5.9, 6.1)], (226, 180, 20, 255))
sticker.text(8.4, 3.0, 2.0, "LASER APERTURE", (12, 12, 12, 255))
sticker.text(8.4, 6.2, 1.3, "CLASS 3B  \u2014  AVOID EXPOSURE", (12, 12, 12, 255))
stamp(sticker, (200, -17, -52), surfaces=["metal"], roughness=0.38, worn=0.12, relief=0.04, **RIGHT)

# Rack number stencilled on the cheek panels.
for placing, side in ((LEFT, 1), (RIGHT, -1)):
    number = Canvas(26, 16)
    number.text(13, 8, 12.5, "07", (198, 196, 186, 255), anchor="mm")
    for x in (7.3, 18.7):   # the bridges of a stencil
        number.rect(x - 0.45, 0, x + 0.45, 16, (0, 0, 0, 0))
    stamp(number, (-292, side * 22.8, 12), surfaces=["polymer_grey"], opacity=0.8, roughness=0.6, worn=0.75, **placing)

# The top chamfers, which the shooter looks down on: model name on the left, serial on the right.
for side, text in ((1, "HV-68 WARDEN  //  6.8\u00d743"), (-1, "SN 68-204417  //  MK II")):
    facing = np.array([0.0, side * 0.749, 0.663])
    right = np.array([-side, 0.0, 0.0])
    label = Canvas(74, 5)
    label.text(37, 2.5, 3.0, text, ETCH, anchor="mm", spacing=0.25)
    stamp(label, (-286, side * 15.25, 35.5), right, np.cross(facing, right), surfaces=["metal"], roughness=0.5, metallic=1.0, relief=-0.02)

# Slot numbers along the top rail, read from behind the rifle.
for slot, x0 in enumerate(range(-316, 200, 10)):
    if slot % 4 == 1:
        label = Canvas(12, 4.6)
        label.text(6, 2.3, 2.6, "T%d" % (slot + 1), PAINT, anchor="mm")
        stamp(label, (x0 + 2.6, 0, 50.5), (0, -1, 0), (1, 0, 0), surfaces=["metal"], opacity=0.85, roughness=0.5, worn=0.3, depth=0.8)

# The maker's mark moulded into both sides of the pistol grip, and a suppressor marking along the top of the shroud.
for placing, side in ((LEFT, 1), (RIGHT, -1)):
    badge = Canvas(18, 18)
    badge.circle(9, 9, 8.6, (0, 0, 0, 90))            # a smooth disc for the mark to stand on
    badge.logo(9, 9, 6.2, (40, 40, 42, 255))
    stamp(badge, (-58, side * 15.5, -124), surfaces=["grip"], opacity=0.0, relief=0.25, flatten=True, depth=3.0, **placing)
label = Canvas(92, 4)
label.text(46, 2, 2.2, "HALVARD S68 SUPPRESSOR   6.8 MM   CLEAN EVERY 2000 RDS", ETCH, anchor="mm", spacing=0.15)
stamp(label, (305, 0, 20), (-1, 0, 0), (0, -1, 0), surfaces=["suppressor"], roughness=0.5, metallic=1.0, relief=-0.02, depth=2.5)

# An orange index band round the charging knob.
band = is_kind("steel_dark") & (np.hypot(P[:, 0] - 195.0, P[:, 2] - 13.5) < 6.5) & (P[:, 1] > 35.2) & (P[:, 1] < 37.4)
albedo[band], metal[band], rough[band] = LOOKS["accent"][0], 0.0, 0.5

# ---------------------------------------------------------------------------------------------------------------
# Wear and dirt (after the markings, so that they wear with the finish)
# ---------------------------------------------------------------------------------------------------------------

log("wear and dirt")
edge = np.clip(EDGE / 0.22, 0.0, 1.0)
outward = smoothstep(0.515, 0.56, POINT)
chips = fbm(P, 5.0, 4, seed=41)
wear = smoothstep(0.24, 0.60, edge * outward * (0.35 + 1.5 * chips)) * wear_amount
for name, (colour, roughness, metallic) in WORN.items():
    chosen = (KIND == SURFACE[name]) & (wear > 0)
    albedo[chosen] = lerp(albedo[chosen], np.array(colour, dtype=np.float32), wear[chosen, None])
    rough[chosen] = lerp(rough[chosen], roughness, wear[chosen])
    metal[chosen] = lerp(metal[chosen], metallic, wear[chosen])
soft = is_kind("polymer", "polymer_grey", "grip", "rubber", "accent")
albedo = np.where(soft[:, None], albedo * (1.0 + wear[:, None] * 1.6) + wear[:, None] * 0.012, albedo)
rough -= wear * 0.14 * soft   # rubbed polymer goes shiny

# Fine scratches across the flats, mostly along the length of the rifle.
scratch = smoothstep(0.84, 0.89, noise(P, (38.0, 0.45, 0.45), seed=43)) * smoothstep(0.45, 0.65, fbm(P, 30.0, 2, seed=44))
scratch += smoothstep(0.87, 0.91, noise(P @ np.array([[0.8, 0, 0.6], [0, 1, 0], [-0.6, 0, 0.8]], dtype=np.float32), (30.0, 0.45, 0.45), seed=45)) * 0.7
scratch = np.clip(scratch, 0.0, 1.0) * is_kind("metal", "polymer", "polymer_grey", "steel_dark", "suppressor")
albedo *= (1.0 + scratch * 0.35)[:, None]
rough += scratch * 0.10 * (metal - 0.5) * -2.0 * 0.6   # brighter and glossier on metal, duller on polymer
height -= scratch * 0.006

# Dust and grime settle where a cloth does not reach.
crease = np.clip(1.0 - AO, 0.0, 1.0)
dust = smoothstep(0.30, 0.85, crease * (0.55 + 0.9 * medium)) * 0.42
dust += smoothstep(0.6, 0.8, big) * smoothstep(0.5, 0.9, N[:, 2]) * 0.10    # a little on upward faces
dust *= ~is_kind("glass", "torch_lens", "laser_lens", "screen", "led", "dark_glass", "brass", "copper", "cavity")
dust = np.clip(dust, 0.0, 1.0)
albedo = lerp(albedo, np.array([0.120, 0.105, 0.088], dtype=np.float32), dust[:, None] * 0.75)
rough = lerp(rough, 0.88, dust * 0.8)
metal *= 1.0 - dust * 0.7

# A share of the occlusion goes into the colour: creases read in any light.
albedo *= lerp(1.0, AO ** 0.7, 0.22)[:, None]

# ---------------------------------------------------------------------------------------------------------------
# Things that glow
# ---------------------------------------------------------------------------------------------------------------

log("glow")
# Holographic reticle on the sight lens: ring, centre dot and four ticks.
reticle = Canvas(30, 30, density=40)
RED = (255, 70, 36, 255)
reticle.circle(15, 15, 7.2, outline=RED, width=0.42)
reticle.circle(15, 15, 0.55, colour=RED)
for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
    reticle.line([(15 + dx * 7.2, 15 + dy * 7.2), (15 + dx * 9.6, 15 + dy * 9.6)], 0.42, RED)
stamp(reticle, (18.5, 0, 84), (0, -1, 0), (0, 0, 1), surfaces=["glass"], glow=1.0, two_sided=True, depth=1.5)

# The sight's little display, tilted up at the shooter: rounds left, a bar for each five, battery.
screen = Canvas(16, 9.6, density=40)
TEAL = (70, 240, 215, 255)
screen.text(1.2, 4.9, 4.6, "30", TEAL)
for bar in range(6):
    screen.rect(8.6 + bar * 1.15, 2.4, 9.4 + bar * 1.15, 6.0, TEAL)
screen.rect(8.6, 7.0, 12.4, 8.4, None, outline=TEAL, width=0.2)
screen.rect(8.9, 7.3, 11.2, 8.1, TEAL)
screen.text(1.2, 8.0, 0.9, "RDS", TEAL)
slope = np.array([13.0, 0.0, 7.5]) / math.hypot(13.0, 7.5)
stamp(screen, (-43.2, 0, 67.6), (0, -1, 0), slope, surfaces=["screen"], glow=1.0, depth=1.5)

# Torch: a polished reflector round a small emitter. It glows faintly even when "off" so the lens reads as glass.
lens = is_kind("torch_lens") & (N[:, 0] > 0.5)
radius = np.hypot(P[:, 1] + 4.0, P[:, 2] + 55.0)
rings = 0.5 + 0.5 * np.cos(radius * 2.4)
albedo[lens] = (lerp(0.35, 0.85, rings[lens]) * smoothstep(1.5, 4.0, radius[lens]))[:, None] * np.array([1.0, 0.98, 0.94])
metal[lens] = smoothstep(2.6, 3.4, radius[lens])
rough[lens] = 0.12
emitter = lens & (np.maximum(np.abs(P[:, 1] + 4.0), np.abs(P[:, 2] + 55.0)) < 1.5)
albedo[emitter] = (0.85, 0.72, 0.18)
emissive[lens] = np.array([1.0, 0.93, 0.78]) * (0.10 * np.exp(-radius[lens] / 3.5))[:, None]
emissive[emitter] = (1.0, 0.93, 0.78)
laser = is_kind("laser_lens") & (N[:, 0] > 0.5)
emissive[laser] = np.array([1.0, 0.03, 0.02]) * np.exp(-np.hypot(P[laser, 1] - 11.0, P[laser, 2] + 46.0) / 0.9)[:, None]
emissive[is_kind("led")] = (0.10, 1.0, 0.32)

# ---------------------------------------------------------------------------------------------------------------
# Texel lists back to images
# ---------------------------------------------------------------------------------------------------------------

log("normals")


def image_of(values):
    out = np.zeros((SIZE, SIZE) + values.shape[1:], dtype=np.float32)
    out[ROWS, COLS] = values
    return out


# The normal map is the slope of the relief, measured in real distance along each texture axis, so the lighting
# matches the millimetres above whatever the texel density of an island.
relief = image_of(height * 0.001)
weight = ndimage.gaussian_filter(VALID.astype(np.float32), 0.7)
relief = np.where(VALID, ndimage.gaussian_filter(relief, 0.7) / np.maximum(weight, 1e-4), 0.0)


def slope(axis):
    ahead, behind = np.roll(relief, -1, axis), np.roll(relief, 1, axis)
    span = np.linalg.norm(np.roll(POSITION, -1, axis) - np.roll(POSITION, 1, axis), axis=2)
    same_island = VALID & np.roll(VALID, -1, axis) & np.roll(VALID, 1, axis) & (span < 0.0012) & (span > 1e-6)
    return np.where(same_island, (ahead - behind) / np.maximum(span, 1e-6), 0.0)


slope_u, slope_v = slope(1), slope(0)
texel = np.linalg.norm(np.roll(POSITION, -1, 1) - POSITION, axis=2)[VALID & np.roll(VALID, -1, 1)]
log("texel size %.3f mm (median)" % (float(np.median(texel)) * 1000.0))
normal = np.dstack([-slope_u, -slope_v, np.ones_like(slope_u)])
normal /= np.linalg.norm(normal, axis=2, keepdims=True)
del relief, weight, slope_u, slope_v, texel

log("saving")
os.makedirs(OUT, exist_ok=True)
# Texels outside the islands take the nearest painted texel, so nothing dark bleeds in when the texture is filtered.
nearest = ndimage.distance_transform_edt(~VALID, return_distances=False, return_indices=True)


def save(name, picture):
    picture = picture[nearest[0], nearest[1]]
    picture = (np.clip(picture, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)[::-1]   # row 0 of the buffers is the bottom
    Image.fromarray(picture).save(os.path.join(OUT, "T_MercRifle_%s.png" % name))
    Image.fromarray(picture).resize((1024, 1024), Image.LANCZOS).save(os.path.join(WORK, "preview_%s.png" % name))


save("BaseColor", image_of(to_srgb(albedo)))
save("Normal", np.where(VALID[:, :, None], normal * 0.5 + 0.5, np.array([0.5, 0.5, 1.0], dtype=np.float32)))
occlusion = lerp(1.0, AO, 0.85)   # never fully black: hidden faces can come into view when parts move
save("ORM", image_of(np.stack([occlusion, np.clip(rough, 0.03, 1.0), np.clip(metal, 0.0, 1.0)], axis=1)))
save("Emissive", image_of(to_srgb(emissive)))
log("done")
