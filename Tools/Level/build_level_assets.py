"""Builds the materials of the level's gameplay markers (Source/OpenSCCT/Play/ScctMarkers*). Runs inside the
editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="<full path>/build_level_assets.py [--force]"
(forward slashes in the path: the script switch reads a backslash as an escape)

The materials go to /Game/OpenSCCT/Level/ and are committed: they are ours, made from nothing but maths.
  M_ScctBeam     additive unlit glow for a round shape seen from the side: brightest where the surface faces the
                 camera and fading to nothing at its outline, so a thin cylinder reads as a soft beam of light and a
                 sphere as a small lamp. Parameters Color, Intensity, Sharpness (how fast it fades to the outline)
                 and Shimmer (0..1: how much the dust in the beam flickers along it).
  M_ScctMarker   additive unlit holographic sign drawn on a quad: a broken outer ring, a progress arc, and a glyph
                 in the middle. Parameters Color, Intensity, Progress (0..1 of the arc), Pulse (0..1: how strongly
                 it throbs) and Shape (0 diamond, 1 square, 2 circle).
An existing material is rebuilt only with --force.
"""
import sys

import unreal

FORCE = "--force" in sys.argv
FOLDER = "/Game/OpenSCCT/Level"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def connect(source, output, target, pin):
    """Wires two nodes; a pin name the engine does not know is reported, not silently left open."""
    if not lib.connect_material_expressions(source, output, target, pin):
        log("NOT CONNECTED: %s -> %s pin '%s'" % (source.get_name(), target.get_name(), pin))


def connect_output(source, output, material_property):
    if not lib.connect_material_property(source, output, material_property):
        log("NOT CONNECTED: %s -> %s" % (source.get_name(), material_property))


def new_material(name):
    path = FOLDER + "/" + name
    if editor_assets.does_asset_exist(path):
        if not FORCE:
            return None
        editor_assets.delete_asset(path)
    material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    return material


def finish(material):
    lib.recompile_material(material)
    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
    log("material built: " + material.get_path_name())


def scalar(material, name, default, x, y):
    node = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def colour(material, name, default, x, y):
    node = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(default[0], default[1], default[2], 1.0))
    return node


def custom(material, code, inputs, x, y):
    """A node of shader code with one float out; inputs is [(pin name, source node)]."""
    node = lib.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for name, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    node.set_editor_property("inputs", pins)
    for name, source in inputs:
        connect(source, "", node, name)
    return node


def multiply(material, a, b, x, y):
    node = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, x, y)
    connect(a, "", node, "A")
    connect(b, "", node, "B")
    return node


def build_beam():
    material = new_material("M_ScctBeam")
    if not material:
        return

    normal = lib.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -1300, 0)
    camera = lib.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -1300, 150)
    position = lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1300, 300)
    time = lib.create_material_expression(material, unreal.MaterialExpressionTime, -1300, 450)
    sharpness = scalar(material, "Sharpness", 2.0, -1300, 600)
    shimmer = scalar(material, "Shimmer", 0.0, -1300, 700)
    # Facing the camera: 1 along the middle of a cylinder seen from the side, 0 at its outline. Dust drifting
    # through the beam is two slow waves along it that never line up.
    glow = custom(material,
                  "float facing = pow(saturate(abs(dot(normalize(N), normalize(V)))), Sharpness);\n"
                  "float dust = 0.5 + 0.25 * sin(dot(P, float3(0.071, 0.063, 0.057)) - T * 1.7)"
                  " + 0.25 * sin(dot(P, float3(0.19, 0.23, 0.17)) + T * 2.9);\n"
                  "return facing * lerp(1.0, dust * 1.6, saturate(Shimmer));",
                  [("N", normal), ("V", camera), ("P", position), ("T", time), ("Sharpness", sharpness), ("Shimmer", shimmer)],
                  -900, 200)

    tint = colour(material, "Color", (1.0, 0.05, 0.03), -900, -200)
    intensity = scalar(material, "Intensity", 4.0, -900, -20)
    light = multiply(material, tint, intensity, -600, -100)
    shaped = multiply(material, light, glow, -400, 50)
    # It dims where it meets a wall instead of ending in a hard ring.
    fade = lib.create_material_expression(material, unreal.MaterialExpressionDepthFade, -600, 350)
    fade.set_editor_property("fade_distance_default", 3.0)
    soft = multiply(material, shaped, fade, -200, 150)
    connect_output(soft, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


MARKER_CODE = """
float2 p = UV * 2.0 - 1.0;
float r = length(p);
float aa = max(fwidth(r), 0.004) * 1.5;
float angle = atan2(p.x, -p.y);
float turn = frac(angle / 6.2831853 + 1.0);

// The outer ring, open at its four diagonals.
float ring = smoothstep(aa, 0.0, abs(r - 0.86) - 0.016) * smoothstep(0.16, 0.28, abs(cos(angle * 2.0)));

// The progress arc: a faint track that fills clockwise from the top.
float track = smoothstep(aa, 0.0, abs(r - 0.68) - 0.04);
float arc = track * (0.16 + 0.84 * step(turn, Progress)) * step(0.0001, Progress);

// The glyph: an outline with a solid heart.
float d = Shape < 0.5 ? abs(p.x) + abs(p.y) : (Shape < 1.5 ? max(abs(p.x), abs(p.y)) * 1.3 : r * 1.25);
float glyph = smoothstep(aa * 2.0, 0.0, abs(d - 0.36) - 0.028) + smoothstep(0.17 + aa * 2.0, 0.17, d) * 0.9;

float halo = pow(saturate(1.0 - r), 3.0) * 0.10;
float scan = 0.84 + 0.16 * sin(p.y * 36.0 + T * 5.0);
float throb = 1.0 + saturate(Pulse) * 0.45 * sin(T * 7.0);
return (ring * 0.85 + arc * 1.3 + glyph + halo) * scan * throb * step(r, 1.0);
"""


def build_marker():
    material = new_material("M_ScctMarker")
    if not material:
        return
    material.set_editor_property("two_sided", True)

    uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1300, 0)
    time = lib.create_material_expression(material, unreal.MaterialExpressionTime, -1300, 150)
    progress = scalar(material, "Progress", 0.0, -1300, 300)
    pulse = scalar(material, "Pulse", 0.0, -1300, 400)
    shape = scalar(material, "Shape", 0.0, -1300, 500)
    sign = custom(material, MARKER_CODE,
                  [("UV", uv), ("T", time), ("Progress", progress), ("Pulse", pulse), ("Shape", shape)], -900, 200)

    tint = colour(material, "Color", (0.25, 0.85, 1.0), -900, -200)
    intensity = scalar(material, "Intensity", 3.0, -900, -20)
    light = multiply(material, tint, intensity, -600, -100)
    shaped = multiply(material, light, sign, -400, 50)
    # It thins out where it cuts into the thing it marks instead of being sliced by it.
    fade = lib.create_material_expression(material, unreal.MaterialExpressionDepthFade, -600, 350)
    fade.set_editor_property("fade_distance_default", 6.0)
    soft = multiply(material, shaped, fade, -200, 150)
    connect_output(soft, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


build_beam()
build_marker()
log("level assets done")
