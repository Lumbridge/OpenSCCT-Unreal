"""Builds the post-process material of the poison overlay's brightening layer.

Runs inside the editor's Python (Tools/Materials/Build-PoisonMaterial.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_poison_material.py"

Writes /Game/OpenSCCT/UI/Vision/M_ScctPoisonDodge. While the player is poisoned the original's health gauge plays two
screen-sized pictures (spec/hud/poison.md in the simulation's repository): effet01, alpha blended, which the HUD paints
itself, and effet02_dodge, which the original blends as "Dodge": the picture under it times (1 + S), S being the
layer's texture times its colour, in the colours of the screen. Slate has no such blend, so that layer is this
material: one Custom node over the finished picture (after tonemapping, so in the screen's colours as the original).
Source/OpenSCCT/Hud/ScctPoisonOverlay sets the parameters:

  Amount   0 leaves the picture alone, 1 is the layer;
  Pattern  the layer's texture (Hud.stickshock, read from the player's content at run time);
  Tint     the layer's colour times the health area's, 0 to 1 as the screen shows them;
  Rect     the layer's left, top, right and bottom as fractions of the view; nothing outside it is touched.

The shader text below is the whole look: edit it here and run the script again; the asset is rebuilt from nothing.
"""
import unreal

PACKAGE = "/Game/OpenSCCT/UI/Vision"
ASSET = "M_ScctPoisonDodge"

# The texture is an sRGB one, so the sample comes back linear and is put back into the screen's colours first.
DODGE = """
float3 C = Scene.rgb;
float2 VUV = GetViewportUV(Parameters);
float2 Span = max(Rect.zw - Rect.xy, 0.00001);
float2 At = (VUV - Rect.xy) / Span;
if (Amount <= 0.0 || At.x < 0.0 || At.x > 1.0 || At.y < 0.0 || At.y > 1.0)
{
    return C;
}
float3 P = Texture2DSampleLevel(Pattern, PatternSampler, At, 0).rgb;
float3 Low = P * 12.92;
float3 High = 1.055 * pow(max(P, 0.0000001), 1.0 / 2.4) - 0.055;
P = lerp(High, Low, step(P, 0.0031308));
float3 S = saturate(P * Tint.rgb);
return lerp(C, min(C * (1.0 + S), 1.0), saturate(Amount));
"""


def log(text):
    unreal.log_warning("SCCT-POISON " + text)


def build():
    path = "%s/%s" % (PACKAGE, ASSET)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    library = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    # After the goggles' materials (priority 0) and before the double vision (10), which doubles the finished frame.
    material.set_editor_property("blendable_priority", 5)

    scene = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, 0)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    inputs = [("Scene", scene, "Color")]

    amount = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 200)
    amount.set_editor_property("parameter_name", "Amount")
    amount.set_editor_property("default_value", 0.0)
    inputs.append(("Amount", amount, ""))

    pattern = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 350)
    pattern.set_editor_property("parameter_name", "Pattern")
    inputs.append(("Pattern", pattern, ""))

    # A vector parameter's first output is its RGB only, so each is joined with its A into a float4 for the shader.
    for index, parameter in enumerate(["Tint", "Rect"]):
        y = 600 + index * 200
        node = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1000, y)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        append = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -700, y)
        if not library.connect_material_expressions(node, "", append, "A") or not library.connect_material_expressions(node, "A", append, "B"):
            log("could not join %s" % parameter)
        inputs.append((parameter, append, ""))

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 300)
    custom.set_editor_property("description", "ScctPoisonDodge")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for pin_name, _, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", DODGE.strip())

    for pin_name, node, output in inputs:
        if not library.connect_material_expressions(node, output, custom, pin_name):
            log("could not connect %s" % pin_name)
    library.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    log("built %s" % path)


build()
log("done")
