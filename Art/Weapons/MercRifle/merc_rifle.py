"""Builds the merc's rifle (the fictional Halvard HV-68 "Warden" bullpup carbine) from nothing. Runs in Blender:

    blender --background --factory-startup --python merc_rifle.py -- <stage> <work folder>

Stages:
    clay    model the rifle and render it with flat materials (quick look at shape and proportions)
    bake    model, unwrap, bake the geometry buffers the texture painter needs, save MercRifle.blend
    finish  open MercRifle.blend, put the painted textures on, render the beauty shots, export the FBX files

The painting itself is paint_textures.py (plain Python with numpy), run between "bake" and "finish".

Model space: 1 unit = 1 m, barrel along +X, up +Z, the shooter's left is +Y. The origin is the middle of the pistol
grip, where the right hand holds the rifle. All dimensions below are in millimetres in a design frame whose origin
is on the bore axis above the trigger; ORIGIN moves that frame to the grip.
"""
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
STAGE = ARGS[0] if ARGS else "clay"
# The bake buffers are large and can be remade: they go to the project's Saved folder, which Git ignores.
WORK = ARGS[1] if len(ARGS) > 1 else os.path.normpath(os.path.join(HERE, "..", "..", "..", "Saved", "MercRifle"))
BAKED = os.path.join(WORK, "MercRifle_baked.blend")   # unwrapped, before texturing
BLEND = os.path.join(HERE, "MercRifle.blend")         # the finished, rigged rifle
RIG_JSON = os.path.join(HERE, "rifle_rig.json")
TEXTURES = os.path.join(HERE, "textures")
RENDERS = os.path.join(HERE, "renders")
BAKE_SIZE = 4096

MM = 0.001
ORIGIN = (-46.0, 0.0, -95.0)  # the middle of the pistol grip, in the design frame

# Surface kinds. The number is what the painter reads back from the baked id map; the colour, metallic and
# roughness are only for the clay renders.
MATERIALS = {
    "metal":        (1, (0.045, 0.048, 0.052), 1.0, 0.42),   # anodised receiver, rail, sight
    "polymer":      (2, (0.030, 0.031, 0.032), 0.0, 0.55),   # dark furniture
    "polymer_grey": (3, (0.150, 0.158, 0.160), 0.0, 0.60),   # lighter panels
    "grip":         (4, (0.022, 0.022, 0.022), 0.0, 0.80),   # stippled grip surfaces
    "rubber":       (5, (0.015, 0.015, 0.015), 0.0, 0.90),
    "steel_dark":   (6, (0.060, 0.060, 0.062), 1.0, 0.35),   # pins, screws, levers
    "steel_bright": (7, (0.620, 0.610, 0.580), 1.0, 0.22),   # bolt carrier
    "suppressor":   (8, (0.105, 0.085, 0.065), 1.0, 0.48),   # bronze-grey shroud
    "brass":        (9, (0.880, 0.640, 0.300), 1.0, 0.28),
    "copper":       (10, (0.800, 0.420, 0.260), 1.0, 0.30),
    "accent":       (11, (0.900, 0.280, 0.030), 0.0, 0.50),  # signal orange
    "cavity":       (12, (0.008, 0.008, 0.008), 0.0, 0.90),  # floors of slots and ports
    "torch_lens":   (13, (0.700, 0.700, 0.650), 0.0, 0.10),
    "laser_lens":   (14, (0.300, 0.020, 0.020), 0.0, 0.10),
    "screen":       (15, (0.010, 0.020, 0.025), 0.0, 0.08),
    "led":          (16, (0.100, 0.800, 0.300), 0.0, 0.30),
    "dark_glass":   (17, (0.010, 0.010, 0.012), 0.0, 0.05),
    "glass":        (18, (0.500, 0.650, 0.700), 0.0, 0.03),  # sight lens, magazine windows: its own material slot
}

# Bones: name -> (parent, head in the design frame). Every bone has the model's own axes (no rotation), so the
# game code moves parts along plain X/Y/Z.
BONES = {
    "Root":        (None, ORIGIN),
    "Magazine":    ("Root", (-206, 0, -64)),   # drops along -Z
    "Bolt":        ("Root", (195, 0, 13.5)),     # charging handle and bolt carrier slide along -X (70 mm)
    "Trigger":     ("Root", (10, 0, -39)),     # turns about Y
    "Selector":    ("Root", (-28, 0, -26)),    # turns about Y
    "Optic":       ("Root", (-6, 0, 50.5)),    # the holographic sight
    "Light":       ("Root", (200, 0, -36)),    # the light and laser module
    "ForeGrip":    ("Root", (100, 0, -49)),    # turns about Y to fold forward under the handguard
}

# Sockets: name -> (parent bone, position in the design frame).
SOCKETS = {
    "Muzzle":       ("Root", (396, 0, 0)),
    "Sight":        ("Optic", (18, 0, 84)),            # centre of the holographic window
    "LightModule":  ("Light", (252, -4, -55)),   # torch lens
    "LaserModule":  ("Light", (241, 11, -46)),   # laser aperture
    "MagWell":      ("Root", (-206, 0, -64)),
    "EjectionPort": ("Root", (-207, -21, 8)),          # cases leave towards -Y in Blender (the shooter's right)
    "LeftHandGrip": ("ForeGrip", (100, 0, -82)),
}

PARTS = []    # the visible parts, in build order
SCREWS = []   # screw heads, for the painter to stamp a hex socket into each
_materials = {}
_counter = [0]


def to_model(co):
    return Vector(((co[0] - ORIGIN[0]) * MM, (co[1] - ORIGIN[1]) * MM, (co[2] - ORIGIN[2]) * MM))


def material(name):
    if name not in _materials:
        index, colour, metallic, roughness = MATERIALS[name]
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat["surface_id"] = index
        shader = mat.node_tree.nodes["Principled BSDF"]
        shader.inputs["Base Color"].default_value = (*colour, 1.0)
        shader.inputs["Metallic"].default_value = metallic
        shader.inputs["Roughness"].default_value = roughness
        if name == "glass":
            shader.inputs["Alpha"].default_value = 0.25
        if name in ("torch_lens", "led", "laser_lens"):
            shader.inputs["Emission Color"].default_value = (*colour, 1.0)
            shader.inputs["Emission Strength"].default_value = 2.0
        _materials[name] = mat
    return _materials[name]


# ---------------------------------------------------------------------------------------------------------------
# Geometry helpers. Each g_* adds one closed solid to a bmesh, in design-frame millimetres.
# ---------------------------------------------------------------------------------------------------------------

PLANES = {
    "XZ": lambda a, b, t: (a, t, b),   # outline drawn from the side, thickness across the rifle
    "YZ": lambda a, b, t: (t, a, b),   # outline drawn from the front, thickness along the barrel
    "XY": lambda a, b, t: (a, b, t),   # outline drawn from above
}


def polygon_area(points):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))


def inset(points, distance):
    """The outline moved inwards by a distance (mitred corners). Used for the big chamfers on slab edges."""
    if distance == 0:
        return list(points)
    if polygon_area(points) < 0:
        distance = -distance
    result = []
    count = len(points)

    def inward(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        return -dy / length, dx / length

    for i in range(count):
        n1 = inward(points[i - 1], points[i])
        n2 = inward(points[i], points[(i + 1) % count])
        scale = max(1.0 + n1[0] * n2[0] + n1[1] * n2[1], 0.35)  # limits the spike on a very sharp corner
        result.append((points[i][0] + distance * (n1[0] + n2[0]) / scale, points[i][1] + distance * (n1[1] + n2[1]) / scale))
    return result


def rrect(width, height, radius, cx=0.0, cy=0.0, steps=4):
    """A rounded rectangle outline, counter-clockwise."""
    points = []
    corners = [(cx + width / 2 - radius, cy + height / 2 - radius, 0), (cx - width / 2 + radius, cy + height / 2 - radius, 90),
               (cx - width / 2 + radius, cy - height / 2 + radius, 180), (cx + width / 2 - radius, cy - height / 2 + radius, 270)]
    for x, y, start in corners:
        for step in range(steps + 1):
            angle = math.radians(start + 90.0 * step / steps)
            points.append((x + radius * math.cos(angle), y + radius * math.sin(angle)))
    return points


def _vert(bm, co, xform):
    co = Vector(co)
    return bm.verts.new(xform @ co if xform else co)


def g_slab(bm, points, t0, t1, plane="XZ", chamfer=0.0, xform=None):
    """An outline given thickness from t0 to t1. A chamfer cuts the rim of each flat side back at 45 degrees:
    (at t0, at t1), or one number for both."""
    low, high = chamfer if isinstance(chamfer, tuple) else (chamfer, chamfer)
    if t0 > t1:
        t0, t1, low, high = t1, t0, high, low
    rings = [(t0, inset(points, low)), (t0 + low, points)] if low else [(t0, points)]
    rings += [(t1 - high, points), (t1, inset(points, high))] if high else [(t1, points)]
    place = PLANES[plane]
    loops = [[_vert(bm, place(a, b, t), xform) for a, b in ring] for t, ring in rings]
    bm.faces.new(loops[0])
    bm.faces.new(loops[-1])
    for lower, upper in zip(loops, loops[1:]):
        count = len(lower)
        for i in range(count):
            bm.faces.new((lower[i], lower[(i + 1) % count], upper[(i + 1) % count], upper[i]))


def g_ring(bm, outer, inner, t0, t1, plane="XZ", xform=None):
    """A frame: the outer outline with the inner outline cut through it. Both have the same number of points."""
    place = PLANES[plane]
    loops = [[_vert(bm, place(a, b, t), xform) for a, b in ring] for t in (t0, t1) for ring in (outer, inner)]
    outer0, inner0, outer1, inner1 = loops
    count = len(outer)
    for i in range(count):
        j = (i + 1) % count
        bm.faces.new((outer0[i], outer0[j], inner0[j], inner0[i]))
        bm.faces.new((outer1[i], outer1[j], inner1[j], inner1[i]))
        bm.faces.new((outer0[i], outer0[j], outer1[j], outer1[i]))
        bm.faces.new((inner0[i], inner0[j], inner1[j], inner1[i]))


def g_lathe(bm, profile, centre, axis="X", segments=24, cap_start=True, cap_end=True, xform=None):
    """A profile of (position along the axis, radius) turned about an axis through centre (the two other
    coordinates, in X, Y, Z order)."""
    def place(t, a, b):
        return {"X": (t, a, b), "Y": (a, t, b), "Z": (a, b, t)}[axis]

    rings = []
    for t, radius in profile:
        ring = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            ring.append(_vert(bm, place(t, centre[0] + radius * math.cos(angle), centre[1] + radius * math.sin(angle)), xform))
        rings.append(ring)
    for lower, upper in zip(rings, rings[1:]):
        for i in range(segments):
            bm.faces.new((lower[i], lower[(i + 1) % segments], upper[(i + 1) % segments], upper[i]))
    if cap_start:
        bm.faces.new(rings[0])
    if cap_end:
        bm.faces.new(rings[-1])


def g_loft(bm, rings, xform=None):
    """A skin over a stack of outlines with the same number of points each (design-frame points), capped at both
    ends. Used for the parts a hand wraps round, which must not look cut from plate."""
    loops = [[_vert(bm, co, xform) for co in ring] for ring in rings]
    for lower, upper in zip(loops, loops[1:]):
        count = len(lower)
        for i in range(count):
            bm.faces.new((lower[i], lower[(i + 1) % count], upper[(i + 1) % count], upper[i]))
    bm.faces.new(loops[0])
    bm.faces.new(loops[-1])


def fillet(points, radius, steps=4):
    """The outline with every corner rounded off to a radius (less where an edge is too short for it)."""
    result = []
    count = len(points)
    for i in range(count):
        corner = Vector(points[i])
        to_previous, to_next = Vector(points[i - 1]) - corner, Vector(points[(i + 1) % count]) - corner
        reach = min(radius, to_previous.length * 0.45, to_next.length * 0.45)
        start, end = corner + to_previous.normalized() * reach, corner + to_next.normalized() * reach
        for step in range(steps + 1):
            t = step / steps
            # A quadratic curve from one edge to the next, pulled towards the corner.
            result.append(tuple((1 - t) * (1 - t) * start + 2 * t * (1 - t) * corner + t * t * end))
    return result


def curve(keys, at):
    """Reads a (position, value) table at the given positions: straight between the keys, then eased, so a
    lofted part has no kinks where the keys are."""
    xs, ys = zip(*sorted(keys))
    values = np.interp(at, xs, ys)
    for _ in range(2):
        values[1:-1] = 0.25 * values[:-2] + 0.5 * values[1:-1] + 0.25 * values[2:]
    return values


def add(name, bm, surface, bone="Root", bevel=0.8, segments_bevel=2, cutter=False, round_part=False):
    """Turns a bmesh into an object. bevel is the width in millimetres of the rounding every hard edge gets, in
    segments_bevel steps; a round part (turned on the lathe) keeps smooth normals round its curve."""
    for vert in bm.verts:
        vert.co = to_model(vert.co)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    _counter[0] += 1
    mesh = bpy.data.meshes.new("%s_%03d" % (name, _counter[0]))
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material(surface))
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj["bone"], obj["bevel"], obj["segments"], obj["round"] = bone, bevel, segments_bevel, round_part
    if not cutter:
        PARTS.append(obj)
    return obj


def slab(name, points, t0, t1, surface, plane="XZ", chamfer=0.0, **options):
    bm = bmesh.new()
    g_slab(bm, points, t0, t1, plane, chamfer)
    return add(name, bm, surface, **options)


def box(name, xs, ys, zs, surface, chamfer=0.0, **options):
    return slab(name, [(xs[0], zs[0]), (xs[1], zs[0]), (xs[1], zs[1]), (xs[0], zs[1])], ys[0], ys[1], surface, chamfer=chamfer, **options)


def side_slab(name, points, side, inner, outer, surface, chamfer=0.0, **options):
    """A plate on one side of the rifle (side +1 is the left), chamfered on its outer face only."""
    return slab(name, points, side * inner, side * outer, surface, chamfer=(0.0, chamfer), **options)


def lathe(name, profile, centre, surface, axis="X", segments=24, cap_start=True, cap_end=True, **options):
    bm = bmesh.new()
    g_lathe(bm, profile, centre, axis, segments, cap_start, cap_end)
    return add(name, bm, surface, round_part=True, **options)


def loft(name, rings, surface, **options):
    bm = bmesh.new()
    g_loft(bm, rings)
    return add(name, bm, surface, round_part=True, **options)


def cutter(build, surface="cavity"):
    """A solid to cut out of another part. The faces it leaves behind get its surface: the dark 'cavity' for the
    floor of a slot or port."""
    bm = bmesh.new()
    build(bm)
    return add("Cutter", bm, surface, cutter=True)


def apply_modifiers(obj):
    """Replaces the object's mesh with what its modifiers make of it (no operators: there is no window)."""
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=bpy.context.evaluated_depsgraph_get())
    old = obj.data
    obj.modifiers.clear()
    obj.data = mesh
    mesh.name = old.name
    bpy.data.meshes.remove(old)


def cut(target, cutters):
    for tool in cutters:
        modifier = target.modifiers.new("Cut", "BOOLEAN")
        modifier.operation = "DIFFERENCE"
        modifier.solver = "EXACT"
        modifier.material_mode = "TRANSFER"
        modifier.object = tool
    apply_modifiers(target)
    for tool in cutters:
        mesh = tool.data
        bpy.data.objects.remove(tool)
        bpy.data.meshes.remove(mesh)


def screw(x, z, side, surface_y, bone="Root", radius=2.6):
    """A screw head on a side face (side +1 is the left), standing 0.9 mm proud."""
    lathe("Screw", [(side * (surface_y - 0.6), radius), (side * (surface_y + 0.9), radius)], (x, z), "steel_dark",
          axis="Y", segments=12, bone=bone, bevel=0.35, segments_bevel=1)
    SCREWS.append({"position": list(to_model((x, side * (surface_y + 0.9), z))), "normal": [0, side, 0], "radius": radius * MM})


# ---------------------------------------------------------------------------------------------------------------
# The rifle
# ---------------------------------------------------------------------------------------------------------------

def build_rifle():
    # ---- upper receiver: one machined piece from butt to handguard, carrying the top rail ----------------------
    upper = slab("Upper", [(-21, -24), (21, -24), (21, 29), (9.5, 42), (-9.5, 42), (-21, 29)], -340, 222, "metal", plane="YZ", bevel=1.2)
    tools = [cutter(lambda bm: g_slab(bm, rrect(58, 24, 3, cx=-207, cy=8), -30, -13))]            # ejection port (right)
    for side in (1, -1):
        tools.append(cutter(lambda bm: g_slab(bm, rrect(205, 6, 2.9, cx=-45, cy=11), side * 19.6, side * 30)))  # long flute
    # A row of small vents along both top chamfers: the surface the shooter looks down on.
    for side in (1, -1):
        tilt = Matrix.Diagonal((1, side, 1, 1)) @ Matrix.Rotation(math.radians(41.5), 4, "X")
        for x0 in range(-230, 60, 18):
            if not -56 < x0 < 40:   # the sight's clamp sits there
                tools.append(cutter(lambda bm: g_slab(bm, rrect(12, 3.4, 1.6, cx=x0, cy=16.5), 33.4, 40, xform=tilt)))
    cut(upper, tools)

    box("RailBase", (-318, 206), (-8, 8), (41, 47.2), "metal", bevel=0.5, segments_bevel=1)
    teeth = bmesh.new()
    tooth = [(-7.8, 46.5), (7.8, 46.5), (10.6, 48.3), (10.6, 49.3), (9.4, 50.5), (-9.4, 50.5), (-10.6, 49.3), (-10.6, 48.3)]
    for x0 in range(-316, 200, 10):
        hidden = -52 < x0 < 34 or 148 <= x0 < 194 or -298 <= x0 < -252   # under the sight and the folded iron sights
        if not hidden:
            g_slab(teeth, tooth, x0, x0 + 5.2, "YZ")
    add("RailTeeth", teeth, "metal", bevel=0.3, segments_bevel=1)

    # ---- lower: the polymer shell that wraps the receiver's underside, magazine well and stock -----------------
    lower = slab("Lower", [(-340, -10), (-340, -108), (-268, -108), (-252, -74), (-246, -64), (-166, -64), (-152, -42), (84, -42), (92, -30), (92, -10)],
                 -23.5, 23.5, "polymer", chamfer=5, bevel=1.2)
    # The stock below the action is hollow: an open frame is lighter and gives the rifle its outline.
    cut(lower, [cutter(lambda bm: g_slab(bm, [(-326, -96), (-282, -96), (-266, -64), (-266, -40), (-326, -40)], -40, 40), "polymer")])
    build_pistol_grip()
    # The guard is a thick moulded loop with eased corners, not a bent strip.
    slab("TriggerGuard", fillet([(89, -40), (89, -63), (72, -95), (-24, -95), (-24, -86), (65, -86), (79, -61), (79, -40)], 7.0),
         -9, 9, "polymer", chamfer=3.0, bevel=0.8)
    # A curved blade: hollow on the front face where the finger lies, the tip curling forward.
    slab("Trigger", [(4.5, -40), (4.6, -50), (3.6, -58), (3.2, -66), (4.4, -73), (7.6, -78.6), (11.6, -79.6), (11.2, -76.5), (9.2, -72),
                     (8.2, -66), (8.6, -58), (10.6, -50), (14.2, -43), (15, -40)],
         -4, 4, "steel_dark", chamfer=1.6, bone="Trigger", bevel=0.4, segments_bevel=1)
    bm = bmesh.new()
    g_ring(bm, rrect(84, 51, 6, cx=-206), rrect(60, 28, 3, cx=-206), -71, -63, "XY")
    add("MagWellLip", bm, "polymer_grey", bevel=1.0)
    slab("ButtPad", [(-356, -100), (-350, -108), (-338, -108), (-338, 42), (-350, 42), (-356, 34)], -22, 22, "rubber", chamfer=5, bevel=1.5)
    slab("MagRelease", [(-264, -74), (-250, -66), (-246, -70), (-250, -98), (-257, -100)], -9, 9, "polymer_grey", chamfer=2, bevel=0.6)
    side_slab("BoltRelease", rrect(16, 8, 2, cx=-140, cy=-26), 1, 23, 25.6, "steel_dark", chamfer=0.8, bevel=0.3, segments_bevel=1)
    slab("Deflector", [(-178, -6), (-164, -6), (-168, 20), (-178, 22)], -20, -24.5, "metal", chamfer=(0, 1.5), bevel=0.6)
    for x, z in ((-256, -22), (-125, -24), (45, -24)):
        lathe("Pin", [(-24.6, 3.2), (24.6, 3.2)], (x, z), "steel_dark", axis="Y", segments=16, bevel=0.5, segments_bevel=1)

    for side in (1, -1):
        side_slab("CheekPanel", [(-330, -4), (-250, -4), (-244, 2), (-244, 27), (-330, 27)], side, 20, 22.8, "polymer_grey", chamfer=1.2, bevel=0.5)
        # Sling sockets: a steel cup at the stock and one at the front of the receiver.
        lathe("SlingCup", [(side * 23, 6.5), (side * 24.9, 6.5), (side * 24.9, 4.2), (side * 23.4, 4.2)], (-300, -24), "steel_dark",
              axis="Y", segments=20, bevel=0.3, segments_bevel=1)
        lathe("SlingCup", [(side * 20.5, 6.5), (side * 22.2, 6.5), (side * 22.2, 4.2), (side * 20.9, 4.2)], (74, 10), "steel_dark",
              axis="Y", segments=20, bevel=0.3, segments_bevel=1)
        for x, z in ((-324, 21), (-324, 2), (-250, 21)):
            screw(x, z, side, 22.8, radius=2.1)
        for x, z in ((30, -31), (-100, -31)):
            screw(x, z, side, 23.5)

    # A steel plate bolted over the trunnion on the left, opposite the ejection port: it carries the maker's mark.
    side_slab("TrunnionPlate", rrect(64, 26, 3, cx=-207, cy=8), 1, 20.5, 22.4, "steel_dark", chamfer=0.8, bevel=0.4, segments_bevel=1)
    for x, z in ((-233, -1), (-233, 17), (-181, -1), (-181, 17)):
        screw(x, z, 1, 22.4, radius=1.8)

    # ---- fire selector, both sides -------------------------------------------------------------------------------
    for side in (1, -1):
        lathe("SelectorHub", [(side * 22.5, 5.0), (side * 26.2, 5.0)], (-28, -26), "steel_dark", axis="Y", segments=20,
              bone="Selector", bevel=0.5, segments_bevel=1)
    side_slab("SelectorLever", [(-31, -29.5), (-9, -28.2), (-8, -26), (-9, -23.8), (-31, -22.5)], 1, 24.5, 27.8, "steel_dark",
              chamfer=1.0, bone="Selector", bevel=0.4, segments_bevel=1)
    side_slab("SelectorLever", [(-31, -29), (-17, -28), (-17, -24), (-31, -23)], -1, 24.5, 27.4, "steel_dark",
              chamfer=1.0, bone="Selector", bevel=0.4, segments_bevel=1)

    # ---- magazine: polymer body with a window on each side showing the rounds ----------------------------------
    body = slab("MagBody", [(-234, -30), (-178, -30), (-177, -64), (-172, -130), (-162, -196), (-222, -196), (-230, -130), (-235, -64)],
                -13, 13, "polymer", chamfer=2.5, bone="Magazine", bevel=0.8)
    window = [(-223, -80), (-187, -80), (-181, -130), (-176, -176), (-214, -176), (-219, -130)]
    cut(body, [cutter(lambda bm: g_slab(bm, window, -20, 20))])
    slab("MagCore", inset(window, -3), -3, 3, "cavity", bone="Magazine", bevel=0)
    brass, copper = bmesh.new(), bmesh.new()
    for row in range(9):
        for y, drop in ((6.3, 0.0), (-6.3, -5.0)):   # two staggered columns
            x, z = -205 + 1.25 * row, -88 - 10.0 * row + drop
            if z < -172:
                continue
            g_lathe(brass, [(x - 19, 4.7), (x + 8, 4.7), (x + 11, 3.3), (x + 14, 3.3)], (y, z), "X", 12)
            g_lathe(copper, [(x + 14, 3.1), (x + 18, 2.8), (x + 22, 1.2)], (y, z), "X", 12)
    add("Rounds", brass, "brass", bone="Magazine", bevel=0)
    add("Bullets", copper, "copper", bone="Magazine", bevel=0)
    slab("Follower", [(-215, -172), (-177, -172), (-176, -181), (-214, -181)], -11, 11, "accent", bone="Magazine", bevel=0.3, segments_bevel=1)
    for side in (1, -1):
        side_slab("MagWindow", inset(window, -1), side, 11.8, 12.6, "glass", bone="Magazine", bevel=0)
    slab("MagFloor", [(-228, -190), (-158, -190), (-155, -204), (-225, -204)], -15.5, 15.5, "polymer_grey", chamfer=3, bone="Magazine", bevel=1.0)

    # ---- handguard ---------------------------------------------------------------------------------------------------
    guard = slab("Handguard", [(-13, -42), (13, -42), (24.5, -30), (24.5, 22), (11, 37), (-11, 37), (-24.5, 22), (-24.5, -30)], 86, 250, "polymer", plane="YZ", bevel=1.5)
    tools = [
        cutter(lambda bm: g_slab(bm, [(228, -50), (262, -50), (262, 50), (250, 50)], -40, 40), "polymer"),             # raked nose
        cutter(lambda bm: g_slab(bm, [(146, -36), (262, -36), (262, -60), (138, -60), (138, -43)], -40, 40), "polymer"),  # step for the light
        cutter(lambda bm: g_slab(bm, [(80, 24), (86, 24), (99, 38), (80, 38)], -40, 40), "polymer"),                    # eased rear top
    ]
    for side in (1, -1):
        for x0 in (100, 140, 180):
            tools.append(cutter(lambda bm: g_slab(bm, rrect(32, 8, 3.9, cx=x0 + 16, cy=-23), side * 22.0, side * 30)))   # accessory slots
    tools.append(cutter(lambda bm: g_slab(bm, rrect(116, 9, 3, cx=152, cy=13.5), 19.5, 30)))                              # charging handle track
    for x0 in (108, 140, 172):
        tools.append(cutter(lambda bm: g_slab(bm, [(x0, 6), (x0 + 22, 6), (x0 + 28, 18), (x0 + 6, 18)], -30, -22.0)))  # cooling vents
    cut(guard, tools)
    for side in (1, -1):
        for x, z in ((95, -26), (95, -3), (226, -26), (232, 2)):
            screw(x, z, side, 24.5)

    # ---- integrated suppressor shroud ----------------------------------------------------------------------------------
    shroud = lathe("Shroud", [(224, 23), (247, 23), (250, 20), (366, 20), (367.5, 18.8), (370.5, 18.8), (372, 20), (376, 20), (377.5, 18.8),
                              (380.5, 18.8), (382, 20), (390, 20), (396, 16.5), (396, 12.5), (387, 10.5), (387, 4.5), (350, 4.5)],
                   (0, 0), "suppressor", segments=48, bevel=0.5, segments_bevel=1)
    tools = []
    for index in range(6):
        turn = Matrix.Rotation(math.radians(30 + 60 * index), 4, "X")
        for cx in (270, 330):
            tools.append(cutter(lambda bm: g_slab(bm, rrect(46, 7, 3.4, cx=cx), 17, 27, "XY", xform=turn)))
    cut(shroud, tools)

    # ---- light and laser module under the barrel -----------------------------------------------------------------------
    slab("LightBody", [(164, -36), (236, -36), (236, -68), (229, -75), (172, -75), (164, -67)], -17, 17, "metal", chamfer=4, bone="Light", bevel=1.0)
    lathe("TorchBezel", [(230, 12.5), (249, 12.5), (252, 11.2), (252, 9.8), (247, 9.8)], (-4, -55), "metal", segments=28, cap_end=False,
          bone="Light", bevel=0.4, segments_bevel=1)
    lathe("TorchRing", [(238, 13.1), (241.5, 13.1)], (-4, -55), "accent", segments=28, bone="Light", bevel=0.3, segments_bevel=1)
    lathe("TorchLens", [(244, 9.9), (247.2, 9.9)], (-4, -55), "torch_lens", segments=28, bone="Light", bevel=0)
    lathe("LaserBezel", [(234, 4.6), (241, 4.6), (241, 3.2), (239, 3.2)], (11, -46), "steel_dark", segments=16, cap_end=False,
          bone="Light", bevel=0.3, segments_bevel=1)
    lathe("LaserLens", [(237, 3.25), (239.5, 3.25)], (11, -46), "laser_lens", segments=16, bone="Light", bevel=0)
    lathe("IrBezel", [(234, 4.2), (239, 4.2), (239, 3.0), (238, 3.0)], (11, -62), "steel_dark", segments=16, cap_end=False,
          bone="Light", bevel=0.3, segments_bevel=1)
    lathe("IrLens", [(236, 3.05), (238.3, 3.05)], (11, -62), "dark_glass", segments=16, bone="Light", bevel=0)
    side_slab("LightPad", rrect(20, 10, 2.5, cx=196, cy=-54), 1, 16.5, 18.8, "rubber", chamfer=0.8, bone="Light", bevel=0.3, segments_bevel=1)
    lathe("StatusLed", [(162.6, 1.8), (164.6, 1.8)], (9, -44), "led", segments=12, bone="Light", bevel=0)
    for side in (1, -1):
        for x, z in ((171, -44), (229, -44)):
            screw(x, z, side, 17, bone="Light", radius=2.1)

    # ---- folding front grip ------------------------------------------------------------------------------------------
    slab("GripBracket", [(88, -43), (112, -43), (112, -52), (108, -56), (92, -56), (88, -52)], -12, 12, "metal", chamfer=2, bevel=0.6)
    slab("ForeGrip", [(91, -54), (109, -54), (108.5, -104), (105, -109), (95, -109), (91.5, -104)], -11, 11, "grip", chamfer=4.5, bone="ForeGrip", bevel=1.2)
    lathe("GripPin", [(-13.2, 3.0), (13.2, 3.0)], (100, -49), "steel_dark", axis="Y", segments=16, bevel=0.4, segments_bevel=1)

    # ---- charging handle (left) and the bolt carrier seen through the ejection port ------------------------------------
    box("ChargingSlider", (184, 206), (18, 26.5), (10, 17), "steel_dark", bone="Bolt", bevel=0.5, segments_bevel=1)
    lathe("ChargingKnob", [(25, 4.2), (27.5, 4.2), (28.5, 6.0), (38, 6.0), (40, 4.8)], (195, 13.5), "steel_dark", axis="Y", segments=20,
          bone="Bolt", bevel=0.4, segments_bevel=1)
    box("BoltCarrier", (-238, -176), (-19.5, -14.5), (-2, 18), "steel_bright", bone="Bolt", bevel=0.5, segments_bevel=1)

    # ---- holographic sight -----------------------------------------------------------------------------------------------
    slab("SightBase", [(-50, 50.5), (36, 50.5), (36, 58), (30, 67), (-4, 67), (-4, 62), (-50, 62)], -15, 15, "metal", chamfer=2.5, bone="Optic", bevel=0.8)
    box("SightClamp", (-42, 30), (13, 19), (44, 55), "metal", bone="Optic", bevel=0.6, segments_bevel=1)
    lathe("SightKnob", [(19, 5.5), (26, 5.5)], (-6, 49.5), "steel_dark", axis="Y", segments=20, bone="Optic", bevel=0.5, segments_bevel=1)
    slab("SightHousing", [(-50, 62), (-4, 62), (-4, 71), (-37, 71), (-50, 63.5)], -14, 14, "metal", chamfer=2, bone="Optic", bevel=0.7)
    # The little display sits on the housing's sloped rear face, tilted up at the shooter.
    along = Vector((13.0, 7.5)).normalized()
    out = Vector((-along.y, along.x))
    a = Vector((-47.5, 64.94))
    b = a + along * 10.0
    screen = [tuple(a - out * 0.6), tuple(b - out * 0.6), tuple(b + out * 0.35), tuple(a + out * 0.35)]
    slab("SightScreen", screen, -8, 8, "screen", bone="Optic", bevel=0.15, segments_bevel=1)
    bm = bmesh.new()
    g_ring(bm, rrect(42, 36, 7, cy=84), rrect(33, 27, 3.5, cy=84), -4, 34, "YZ")
    add("SightHood", bm, "metal", bone="Optic", bevel=0.9)
    slab("SightLens", rrect(33.6, 27.6, 3.5, cy=84), 18, 19, "glass", plane="YZ", bone="Optic", bevel=0)
    lathe("BatteryCap", [(-13.5, 5.2), (-19, 5.2), (-20, 4.2)], (-27, 66), "steel_dark", axis="Y", segments=20, bone="Optic", bevel=0.4, segments_bevel=1)
    for x in (-41, -31):
        side_slab("SightButton", rrect(7, 5, 1.5, cx=x, cy=66.5), 1, 13.5, 15.2, "rubber", chamfer=0.6, bone="Optic", bevel=0.2, segments_bevel=1)
    for side in (1, -1):
        for x, z in ((-43, 56.5), (29, 56.5)):
            screw(x, z, side, 15, bone="Optic", radius=2.1)
        screw(24, 84, side, 21, bone="Optic", radius=2.1)

    # ---- folded back-up iron sights on the rail ----------------------------------------------------------------------
    build_iron_sights()


def build_pistol_grip():
    """The pistol grip, lofted from rounded sections: a beavertail under the receiver, a shelf for the middle
    finger below the trigger guard, a palm swell on the back strap and a flared base. Leans 17 degrees."""
    # (height, x) of the front and back straps and (height, half width), in the design frame.
    front = [(-34, -6), (-44, -9), (-58, -12.5), (-68, -17), (-76, -20.5), (-88, -21.5), (-98, -23.5), (-112, -28), (-128, -33.5), (-144, -39.5), (-156, -43)]
    back = [(-34, -70), (-40, -75), (-46, -73), (-54, -67), (-62, -64.5), (-74, -66.5), (-90, -72), (-110, -80.5), (-130, -88), (-146, -92.5), (-156, -93)]
    width = [(-34, 13.0), (-50, 13.2), (-70, 14.2), (-100, 15.8), (-125, 16.0), (-145, 15.4), (-156, 15.9)]

    def section(z, x_front, x_back, half_width):
        ring = []
        centre, half_length = (x_front + x_back) / 2.0, (x_front - x_back) / 2.0
        for step in range(36):
            angle = 2.0 * math.pi * step / 36
            # A squared-off oval, narrower at the front strap than at the back, as a hand closes round it.
            ux = math.copysign(abs(math.cos(angle)) ** 0.74, math.cos(angle))
            uy = math.copysign(abs(math.sin(angle)) ** 0.74, math.sin(angle))
            narrow = 1.0 - (0.24 if ux > 0 else 0.10) * ux * ux
            ring.append((centre + half_length * ux, half_width * narrow * uy, z))
        return ring

    heights = np.linspace(-34.0, -156.0, 32)
    fronts, backs, widths = curve(front, heights), curve(back, heights), curve(width, heights)
    loft("Grip", [section(z, f, b, w) for z, f, b, w in zip(heights, fronts, backs, widths)], "grip", bevel=1.6, segments_bevel=3)
    # A base plate a little proud of the grip, its lower rim eased.
    f, b, w = fronts[-1] + 1.4, backs[-1] - 1.4, widths[-1] + 1.2
    loft("GripCap", [section(-154.5, f, b, w), section(-160.0, f, b, w), section(-162.0, f - 2.0, b + 2.0, w - 2.0)], "polymer_grey",
         bevel=0.6, segments_bevel=2)


def build_iron_sights():
    """Folded back-up sights on the rail: a clamped steel base, the leaf lying flat on it with its aperture
    upwards, the hinge and (on the rear one) a windage knob. The rear one is the nearest thing to the merc's eye."""
    for x0, rear in ((150, False), (-296, True)):
        slab("IronSightBase", [(x0 + 2, 50.5), (x0 + 44, 50.5), (x0 + 44, 53.4), (x0 + 41.5, 55), (x0 + 4.5, 55), (x0 + 2, 53.4)],
             -10.5, 10.5, "steel_dark", chamfer=1.5, bevel=0.5)
        box("IronSightClamp", (x0 + 8, x0 + 38), (10.5, 13.6), (46.6, 54), "steel_dark", bevel=0.5, segments_bevel=1)
        for x in (x0 + 14, x0 + 32):
            screw(x, 50.4, 1, 13.6, radius=1.8)
        slab("IronSightLeaf", [(x0 + 10, 55), (x0 + 43, 55), (x0 + 43, 57.6), (x0 + 40.5, 60), (x0 + 30, 60), (x0 + 27, 58.4), (x0 + 16, 58.4), (x0 + 13.5, 60), (x0 + 10, 60)],
             -7.5, 7.5, "polymer", chamfer=1.6, bevel=0.5)
        lathe("IronSightHinge", [(-10.5, 3.3), (10.5, 3.3)], (x0 + 8, 57.2), "steel_dark", axis="Y", segments=16, bevel=0.4, segments_bevel=1)
        # The aperture, seen from above while the sight is folded.
        lathe("IronSightAperture", [(59.6, 5.0), (61.4, 5.0), (61.4, 2.4), (60.2, 2.4)], (x0 + 35.2, 0), "steel_dark", axis="Z",
              segments=20, bevel=0.3, segments_bevel=1)
        if rear:
            lathe("WindageKnob", [(7.5, 3.4), (11.5, 3.4), (12.2, 2.6)], (x0 + 21.5, 56.6), "steel_dark", axis="Y", segments=16,
                  bevel=0.3, segments_bevel=1)


def finish_parts():
    """Rounds every hard edge, sets the shading and names the bone each part follows."""
    for obj in PARTS:
        if obj["bevel"] > 0:
            bevel = obj.modifiers.new("Bevel", "BEVEL")
            bevel.width = obj["bevel"] * MM
            bevel.segments = obj["segments"]
            bevel.limit_method = "ANGLE"
            bevel.angle_limit = math.radians(30)
            # Flat faces stay flat and only the bevels curve: the look of machined parts.
            bevel.harden_normals = not obj["round"]
            apply_modifiers(obj)
        mesh = obj.data
        if obj["bevel"] <= 0 or obj["round"]:
            # Turned parts: smooth round the curve, sharp at the ends.
            bm = bmesh.new()
            bm.from_mesh(mesh)
            for face in bm.faces:
                face.smooth = True
            for edge in bm.edges:
                edge.smooth = not (len(edge.link_faces) == 2 and edge.calc_face_angle() > math.radians(50))
            bm.to_mesh(mesh)
            bm.free()
        if obj.name.startswith("Shroud"):
            round_normals(mesh, 0.020)
        group = obj.vertex_groups.new(name=obj["bone"])
        group.add(range(len(obj.data.vertices)), 1.0, "REPLACE")


def round_normals(mesh, radius):
    """Gives the outside of a tube round the bore exact radial normals. The slots cut into the shroud leave long
    thin faces whose smoothed normals would otherwise show as streaks."""
    axis_z = -ORIGIN[2] * MM
    normals = [corner.vector.copy() for corner in mesh.corner_normals]
    for polygon in mesh.polygons:
        for loop in polygon.loop_indices:
            co = mesh.vertices[mesh.loops[loop].vertex_index].co
            radial = Vector((0.0, co.y, co.z - axis_z))
            if abs(radial.length - radius) < 0.0002 and polygon.normal.dot(radial.normalized()) > 0.9:
                normals[loop] = radial.normalized()
    mesh.normals_split_custom_set(normals)


def join(objects, name):
    for obj in bpy.context.scene.objects:
        obj.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = joined.data.name = name
    return joined


def triangle_count(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def remove_buried_faces(obj):
    """Deletes the faces that lie inside another part (a pin's shaft in the receiver, the receiver's underside in
    the lower). They would never be seen, and they would take a third of the texture.

    A face is buried when rays from it, in every direction it faces, first meet the inside of a surface. Only
    parts on the same bone bury each other: a magazine pulled out must still have its top.
    """
    from mathutils.bvhtree import BVHTree
    mesh = obj.data
    points = [vertex.co.copy() for vertex in mesh.vertices]
    bone_of = [vertex.groups[0].group for vertex in mesh.vertices]
    by_bone = {}
    for polygon in mesh.polygons:
        by_bone.setdefault(bone_of[polygon.vertices[0]], []).append(polygon.index)

    # Directions spread evenly over a hemisphere, the first straight out.
    rays = [Vector((0, 0, 1))]
    for i in range(23):
        z = 1.0 - (i + 0.5) / 23.0 * 0.95
        angle = i * 2.399963
        rays.append(Vector((math.sqrt(1 - z * z) * math.cos(angle), math.sqrt(1 - z * z) * math.sin(angle), z)))

    buried = []
    for indices in by_bone.values():
        tree = BVHTree.FromPolygons(points, [tuple(mesh.polygons[i].vertices) for i in indices])
        for i in indices:
            polygon = mesh.polygons[i]
            normal = polygon.normal
            frame = normal.to_track_quat("Z", "Y")
            directions = [frame @ ray for ray in rays]
            seen = False
            for start in [polygon.center] + [polygon.center.lerp(points[v], 0.85) for v in polygon.vertices]:
                origin = start + normal * 0.0002
                inside = 0
                for direction in directions:
                    _, hit_normal, _, _ = tree.ray_cast(origin, direction, 2.0)
                    if hit_normal is not None and hit_normal.dot(direction) > 0:
                        inside += 1
                if inside < len(directions) - 2:
                    seen = True
                    break
            if not seen:
                buried.append(i)

    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.faces[i] for i in buried], context="FACES")
    bm.to_mesh(mesh)
    bm.free()
    print("MERC-RIFLE removed %d buried triangles" % len(buried))


def build_model():
    """The whole rifle as two objects: the body and the glass (sight lens and magazine windows)."""
    build_rifle()
    finish_parts()
    glass = [obj for obj in PARTS if obj.data.materials[0].name == "glass"]
    body = [obj for obj in PARTS if obj not in glass]
    body = join(body, "MercRifle_Body")
    glass = join(glass, "MercRifle_Glass")
    for obj in (body, glass):
        triangulate = obj.modifiers.new("Triangulate", "TRIANGULATE")
        triangulate.quad_method = "BEAUTY"
        triangulate.ngon_method = "BEAUTY"
        apply_modifiers(obj)
    remove_buried_faces(body)
    print("MERC-RIFLE triangles: body %d, glass %d" % (triangle_count(body), triangle_count(glass)))
    return body, glass


# ---------------------------------------------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------------------------------------------

def use_gpu():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    try:
        preferences = bpy.context.preferences.addons["cycles"].preferences
        preferences.compute_device_type = "OPTIX"
        preferences.get_devices()
        for device in preferences.devices:
            device.use = device.type == "OPTIX"
        scene.cycles.device = "GPU"
    except Exception as error:  # a machine without the card still renders, slowly
        print("MERC-RIFLE no GPU rendering:", error)


def look_at(obj, eye, target):
    obj.location = eye
    obj.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()


def area_light(name, eye, target, size, watts, colour):
    data = bpy.data.lights.new(name, "AREA")
    data.size, data.energy, data.color = size, watts, colour
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    look_at(obj, eye, target)


# name: (eye, target, lens in mm). The first-person view is where the merc's eye is when the rifle is at the ready.
VIEWS = {
    "side_left": ((0.07, 1.35, 0.06), (0.07, 0.0, 0.03), 80),
    "side_right": ((0.07, -1.35, 0.06), (0.07, 0.0, 0.03), 80),
    "three_quarter": ((0.80, 0.62, 0.34), (0.08, 0.0, 0.03), 60),
    "rear_quarter": ((-0.72, -0.55, 0.36), (0.04, 0.0, 0.03), 60),
    "first_person": ((-0.42, 0.14, 0.215), (0.50, 0.02, 0.12), 24),
    "detail_sight": ((0.24, 0.30, 0.30), (0.0, 0.0, 0.13), 85),
    "detail_muzzle": ((0.62, 0.22, 0.02), (0.30, 0.0, 0.06), 70),
}


def setup_studio():
    scene = bpy.context.scene
    use_gpu()
    scene.cycles.samples = 96
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    try:
        scene.view_settings.look = "AgX - Medium High Contrast"
    except TypeError:
        pass

    # Blender's own studio environment (CC0, shipped with Blender) gives the metal something to reflect.
    world = bpy.data.worlds.new("Studio")
    world.use_nodes = True
    scene.world = world
    nodes, links = world.node_tree.nodes, world.node_tree.links
    environment = nodes.new("ShaderNodeTexEnvironment")
    version = "%d.%d" % bpy.app.version[:2]
    environment.image = bpy.data.images.load(os.path.join(os.path.dirname(bpy.app.binary_path), version, "datafiles", "studiolights", "world", "studio.exr"))
    links.new(environment.outputs["Color"], nodes["Background"].inputs["Color"])
    nodes["Background"].inputs["Strength"].default_value = 0.55
    world.cycles_visibility.camera = False

    floor_mesh = bpy.data.meshes.new("Backdrop")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=6.0)
    bm.to_mesh(floor_mesh)
    bm.free()
    floor = bpy.data.objects.new("Backdrop", floor_mesh)
    floor.location = (0, 0, -0.22)
    scene.collection.objects.link(floor)
    backdrop = bpy.data.materials.new("Backdrop")
    backdrop.use_nodes = True
    backdrop.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.02, 0.022, 0.025, 1)
    backdrop.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.35
    floor_mesh.materials.append(backdrop)

    area_light("Key", (0.5, 1.2, 1.3), (0.05, 0, 0.05), 1.2, 70, (1.0, 0.96, 0.9))
    area_light("KeyRight", (0.3, -1.3, 1.1), (0.05, 0, 0.05), 1.2, 60, (1.0, 0.96, 0.9))
    area_light("Rim", (-1.2, -0.3, 0.9), (0.0, 0, 0.05), 0.8, 70, (0.6, 0.78, 1.0))
    area_light("Top", (0.1, 0.0, 1.6), (0.1, 0, 0.0), 2.0, 50, (1.0, 1.0, 1.0))

    camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera.data.clip_start = 0.01
    return camera


def render_views(camera, prefix, names=None):
    os.makedirs(RENDERS, exist_ok=True)
    for name, (eye, target, lens) in VIEWS.items():
        if names and name not in names:
            continue
        look_at(camera, eye, target)
        camera.data.lens = lens
        bpy.context.scene.render.filepath = os.path.join(RENDERS, "%s_%s.png" % (prefix, name))
        bpy.ops.render.render(write_still=True)
        print("MERC-RIFLE rendered", bpy.context.scene.render.filepath)


# ---------------------------------------------------------------------------------------------------------------
# Stages
# ---------------------------------------------------------------------------------------------------------------

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def stage_clay():
    reset_scene()
    build_model()
    camera = setup_studio()
    bpy.context.scene.cycles.samples = 48
    render_views(camera, "clay", ARGS[2].split(",") if len(ARGS) > 2 else None)


def select_only(objects):
    for obj in bpy.context.scene.objects:
        obj.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def unwrap(objects):
    """One texture atlas for the whole rifle: every face group gets its own island at the same texel density."""
    select_only(objects)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.0, area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    bpy.ops.uv.pack_islands(rotate=True, margin_method="FRACTION", margin=0.0011)
    bpy.ops.object.mode_set(mode="OBJECT")


def set_bake_nodes(what, image):
    """Rebuilds every material so that baking 'emission' writes the wanted quantity into the image."""
    for mat in bpy.data.materials:
        if "surface_id" not in mat:
            continue
        nodes, links = mat.node_tree.nodes, mat.node_tree.links
        nodes.clear()
        output = nodes.new("ShaderNodeOutputMaterial")
        emission = nodes.new("ShaderNodeEmission")
        links.new(emission.outputs["Emission"], output.inputs["Surface"])
        target = nodes.new("ShaderNodeTexImage")
        target.image = image
        nodes.active = target   # the bake writes into the active image node
        if what == "ids":
            # Red: the surface kind. Blue: 1 wherever the atlas is used.
            emission.inputs["Color"].default_value = (mat["surface_id"] / 32.0, 0.0, 1.0, 1.0)
        elif what == "position":
            geometry = nodes.new("ShaderNodeNewGeometry")
            shift = nodes.new("ShaderNodeVectorMath")   # emission cannot be negative: the model fits in +-1 m
            shift.operation = "ADD"
            shift.inputs[1].default_value = (1.0, 1.0, 1.0)
            links.new(geometry.outputs["Position"], shift.inputs[0])
            links.new(shift.outputs["Vector"], emission.inputs["Color"])
        elif what == "edges":
            # Red: how far the surface normal is from the normal of a rounded-off copy: bright on and near edges,
            # also where two parts meet. Green: pointiness (above a half on outward corners, below in creases).
            geometry = nodes.new("ShaderNodeNewGeometry")
            rounded = nodes.new("ShaderNodeBevel")
            rounded.samples = 8
            rounded.inputs["Radius"].default_value = 0.0025
            dot = nodes.new("ShaderNodeVectorMath")
            dot.operation = "DOT_PRODUCT"
            links.new(geometry.outputs["Normal"], dot.inputs[0])
            links.new(rounded.outputs["Normal"], dot.inputs[1])
            invert = nodes.new("ShaderNodeMath")
            invert.operation = "SUBTRACT"
            invert.inputs[0].default_value = 1.0
            links.new(dot.outputs["Value"], invert.inputs[1])
            combine = nodes.new("ShaderNodeCombineXYZ")
            links.new(invert.outputs["Value"], combine.inputs["X"])
            links.new(geometry.outputs["Pointiness"], combine.inputs["Y"])
            links.new(combine.outputs["Vector"], emission.inputs["Color"])


def bake_buffers(body, glass):
    """Bakes what the painter needs to know about every texel: surface kind, position, normal, occlusion, edges."""
    os.makedirs(WORK, exist_ok=True)
    scene = bpy.context.scene
    use_gpu()
    scene.world = bpy.data.worlds.new("Bake")
    scene.world.light_settings.distance = 0.06   # occlusion looks 6 cm around: parts shade their neighbours
    scene.render.bake.margin = 0
    scene.render.bake.use_clear = False
    image = bpy.data.images.new("Bake", BAKE_SIZE, BAKE_SIZE, alpha=False, float_buffer=True)
    image.colorspace_settings.name = "Non-Color"
    pixels = np.empty(BAKE_SIZE * BAKE_SIZE * 4, dtype=np.float32)

    def run(name, what, bake_type, samples, objects, channels, **options):
        set_bake_nodes(what, image)
        pixels[:] = 0.0
        image.pixels.foreach_set(pixels)
        scene.cycles.samples = samples
        for obj in objects:
            select_only([obj])
            bpy.ops.object.bake(type=bake_type, **options)
        image.pixels.foreach_get(pixels)
        np.save(os.path.join(WORK, name + ".npy"), pixels.reshape(BAKE_SIZE, BAKE_SIZE, 4)[:, :, :channels].copy())
        print("MERC-RIFLE baked", name)

    both = [body, glass]
    run("ids", "ids", "EMIT", 4, both, 3)
    run("position", "position", "EMIT", 4, both, 3)
    run("normal", "ids", "NORMAL", 4, both, 3, normal_space="OBJECT")
    run("edges", "edges", "EMIT", 32, [body], 2)
    # The glass must not shade what is behind it.
    glass.hide_render = True
    run("occlusion", "ids", "AO", 128, [body], 1)
    glass.hide_render = False


def write_rig():
    """What the texture painter and the Unreal import need to know about the model."""
    def unreal(co):   # model metres -> Unreal centimetres (Unreal's Y points the other way)
        return [round(co[0] * 100.0, 4), round(-co[1] * 100.0, 4), round(co[2] * 100.0, 4)]

    heads = {name: to_model(head) for name, (_, head) in BONES.items()}
    rig = {
        "origin_mm": list(ORIGIN),
        "surfaces": {name: values[0] for name, values in MATERIALS.items()},
        "bones": [{"name": name, "parent": parent,
                   "head_m": list(heads[name]),
                   "unreal_cm": unreal(heads[name]),
                   "unreal_local_cm": unreal(heads[name] - heads[parent]) if parent else unreal(heads[name])}
                  for name, (parent, _) in BONES.items()],
        "sockets": [{"name": name, "bone": bone,
                     "unreal_cm": unreal(to_model(position)),
                     "unreal_local_cm": unreal(to_model(position) - heads[bone])}
                    for name, (bone, position) in SOCKETS.items()],
        "screws": SCREWS,
    }
    with open(RIG_JSON, "w", encoding="utf-8") as stream:
        json.dump(rig, stream, indent=1)


def stage_bake():
    reset_scene()
    body, glass = build_model()
    write_rig()
    unwrap([body, glass])
    bake_buffers(body, glass)
    bpy.ops.wm.save_as_mainfile(filepath=BAKED, compress=True)
    print("MERC-RIFLE saved", BAKED)


def textured_material(name, glass):
    """The game material, rebuilt for Blender's renderer from the painted textures."""
    def texture(suffix, colour):
        image = bpy.data.images.load(os.path.join(TEXTURES, "T_MercRifle_%s.png" % suffix), check_existing=True)
        image.colorspace_settings.name = "sRGB" if colour else "Non-Color"
        node = nodes.new("ShaderNodeTexImage")
        node.image = image
        return node

    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    shader = nodes["Principled BSDF"]
    base, orm, normal, emissive = texture("BaseColor", True), texture("ORM", False), texture("Normal", False), texture("Emissive", True)
    split = nodes.new("ShaderNodeSeparateColor")
    links.new(orm.outputs["Color"], split.inputs["Color"])
    links.new(split.outputs["Green"], shader.inputs["Roughness"])
    links.new(split.outputs["Blue"], shader.inputs["Metallic"])
    bump = nodes.new("ShaderNodeNormalMap")
    links.new(normal.outputs["Color"], bump.inputs["Color"])
    links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    links.new(emissive.outputs["Color"], shader.inputs["Emission Color"])
    shader.inputs["Emission Strength"].default_value = 12.0 if glass else 6.0
    if glass:
        links.new(base.outputs["Color"], shader.inputs["Base Color"])
        # Clear except where the reticle glows.
        glow = nodes.new("ShaderNodeRGBToBW")
        links.new(emissive.outputs["Color"], glow.inputs["Color"])
        opacity = nodes.new("ShaderNodeMath")
        opacity.operation = "ADD"
        opacity.use_clamp = True
        opacity.inputs[1].default_value = 0.22
        links.new(glow.outputs["Val"], opacity.inputs[0])
        links.new(opacity.outputs["Value"], shader.inputs["Alpha"])
    else:
        # The renderer has no occlusion input: a little of it goes into the colour.
        shade = nodes.new("ShaderNodeMix")
        shade.data_type = "RGBA"
        shade.blend_type = "MULTIPLY"
        shade.inputs["Factor"].default_value = 0.5
        links.new(base.outputs["Color"], shade.inputs["A"])
        links.new(split.outputs["Red"], shade.inputs["B"])
        links.new(shade.outputs["Result"], shader.inputs["Base Color"])
    return mat


def make_armature(mesh_objects):
    data = bpy.data.armatures.new("Armature")
    # Named "Armature" so that Unreal drops the object itself and takes "Root" as the root bone.
    armature = bpy.data.objects.new("Armature", data)
    bpy.context.scene.collection.objects.link(armature)
    select_only([armature])
    bpy.ops.object.mode_set(mode="EDIT")
    for name, (parent, head) in BONES.items():
        bone = data.edit_bones.new(name)
        bone.head = to_model(head)
        # A bone pointing along +Y with no roll has exactly the model's axes.
        bone.tail = bone.head + Vector((0.0, 0.03, 0.0))
        bone.roll = 0.0
        if parent:
            bone.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    for bone in data.bones:
        rotation = bone.matrix_local.to_3x3()
        assert max(abs(rotation[i][j] - (i == j)) for i in range(3) for j in range(3)) < 1e-4, bone.name
    for obj in mesh_objects:
        obj.parent = armature
        obj.modifiers.new("Armature", "ARMATURE").object = armature
    return armature


def to_centimetres(armature, meshes):
    """Rescales the rig to Unreal's unit just before export. Exported in metres, the root bone would arrive in
    Unreal with a scale of 100 and every other bone's offset in metres, and sockets would land 100 times too far."""
    bpy.context.scene.unit_settings.scale_length = 0.01
    for obj in meshes:
        obj.data.transform(Matrix.Scale(100.0, 4))
    select_only([armature])
    bpy.ops.object.mode_set(mode="EDIT")
    for bone in armature.data.edit_bones:
        bone.head, bone.tail = bone.head * 100.0, bone.tail * 100.0
    bpy.ops.object.mode_set(mode="OBJECT")


def export_fbx(path, objects):
    select_only(objects)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=False, mesh_smooth_type="FACE", use_tspace=False, use_mesh_modifiers=False,
                             primary_bone_axis="Y", secondary_bone_axis="X", axis_forward="-Z", axis_up="Y")
    print("MERC-RIFLE exported", path)


def stage_finish():
    bpy.ops.wm.open_mainfile(filepath=BAKED)
    body, glass = bpy.data.objects["MercRifle_Body"], bpy.data.objects["MercRifle_Glass"]
    for obj, mat in ((body, textured_material("M_MercRifle", False)), (glass, textured_material("M_MercRifleGlass", True))):
        obj.data.materials.clear()
        obj.data.materials.append(mat)
        obj.data.polygons.foreach_set("material_index", [0] * len(obj.data.polygons))
    rifle = join([body, glass], "MercRifle")
    print("MERC-RIFLE triangles: %d" % triangle_count(rifle))

    # The third-person model: the same rifle and textures at about a sixth of the triangles.
    low = rifle.copy()
    low.data = rifle.data.copy()
    low.name = low.data.name = "MercRifle_LOD1"
    bpy.context.scene.collection.objects.link(low)
    decimate = low.modifiers.new("Decimate", "DECIMATE")
    decimate.ratio = 5500.0 / triangle_count(rifle)
    apply_modifiers(low)
    print("MERC-RIFLE third-person triangles: %d" % triangle_count(low))

    armature = make_armature([rifle, low])

    camera = setup_studio()
    wanted = ARGS[2].split(",") if len(ARGS) > 2 else None
    low.hide_render = True
    render_views(camera, "beauty", wanted)
    if not wanted:
        low.hide_render, rifle.hide_render = False, True
        render_views(camera, "lod1", ["three_quarter"])
        low.hide_render, rifle.hide_render = True, False
    bpy.context.preferences.filepaths.save_version = 0   # no .blend1 backup next to the file
    bpy.ops.wm.save_as_mainfile(filepath=BLEND, compress=True)

    # The .blend stays in metres; only the exported copies are in centimetres.
    to_centimetres(armature, [rifle, low])
    export_fbx(os.path.join(HERE, "MercRifle.fbx"), [armature, rifle])
    export_fbx(os.path.join(HERE, "MercRifle_LOD1.fbx"), [armature, low])


if STAGE == "clay":
    stage_clay()
elif STAGE == "bake":
    stage_bake()
elif STAGE == "finish":
    stage_finish()
