"""Builds the materials of the hub's match pads (Source/OpenSCCT/Hub/ScctHubPads*). Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="<full path>/build_hub_assets.py [--force]"
(forward slashes in the path: the script switch reads a backslash as an escape)

The materials go to /Game/OpenSCCT/Hub/ and are committed: they are ours, made from nothing but maths.
  M_ScctHubPad      a pad's floor overlay on a quad lying on the floor: a thin tint over the whole pad and a solid
                    border round it, translucent and lit by the hub's own light (so it reads the same in the sunny
                    plaza and under the eye's night exposure), with a little of its colour glowing in the border.
                    Parameters Color, Fill (opacity inside), Border (opacity of the border), BorderU and BorderV (the
                    border's width as a part of the quad's length and width) and Glow.
  M_ScctHubPicture  the map's picture over a pad's sign: an opaque lit poster (the hub's light falls on it as on the
                    sign board) showing the texture parameter Picture, with a faint glow (Glow) so it is not black in
                    a shadow.
An existing material is rebuilt only with --force.
"""
import sys

import unreal

FORCE = "--force" in sys.argv
FOLDER = "/Game/OpenSCCT/Hub"

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
    return asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


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


# How far into the border a point is: 1 on the border, 0 inside, with a soft inner edge a tenth of its width.
EDGE_CODE = """float2 d = min(UV, 1.0 - UV);
float2 w = max(float2(BorderU, BorderV), 1e-4);
float2 e = 1.0 - smoothstep(w * 0.9, w, d);
return max(e.x, e.y);"""

OPACITY_CODE = "return lerp(Fill, Border, Edge);"


def build_pad():
    material = new_material("M_ScctHubPad")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)

    uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1300, 0)
    border_u = scalar(material, "BorderU", 0.02, -1300, 150)
    border_v = scalar(material, "BorderV", 0.02, -1300, 250)
    edge = custom(material, EDGE_CODE, [("UV", uv), ("BorderU", border_u), ("BorderV", border_v)], -900, 100)

    fill = scalar(material, "Fill", 0.1, -900, 300)
    border = scalar(material, "Border", 0.85, -900, 400)
    opacity = custom(material, OPACITY_CODE, [("Fill", fill), ("Border", border), ("Edge", edge)], -600, 300)
    connect_output(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    tint = colour(material, "Color", (0.8, 0.84, 0.9), -900, -200)
    connect_output(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -600, -40)
    rough.set_editor_property("r", 0.8)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    glow = scalar(material, "Glow", 0.6, -900, -60)
    lit = multiply(material, tint, glow, -600, -150)
    rim = multiply(material, lit, edge, -350, -100)
    connect_output(rim, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


def build_picture():
    material = new_material("M_ScctHubPicture")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("two_sided", True)

    picture = lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    picture.set_editor_property("parameter_name", "Picture")
    default = unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    if default:
        picture.set_editor_property("texture", default)
    connect_output(picture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -600, 200)
    rough.set_editor_property("r", 0.55)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    glow = scalar(material, "Glow", 0.15, -900, 300)
    faint = multiply(material, picture, glow, -500, 120)
    connect(picture, "RGB", faint, "A")
    connect_output(faint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


build_pad()
build_picture()
log("hub assets done")
