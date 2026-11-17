"""Builds the demo X-ray outline: the post-process material that draws every player's contour through walls while a
demo is watched with X-ray on (a port of the Godot client's game/Shaders/svm_demo_xray.gdshader).

Runs inside the editor's Python (Tools/Materials/Build-DemoXRayMaterial.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_demo_xray_material.py"

Writes /Game/OpenSCCT/Demo/M_ScctDemoXRay, rebuilt from nothing each time.

Godot draws the posed bodies alone into a mask (a camera that sees no map), grows each silhouette by two pixels and
takes the silhouette itself away again, so only a rim in the team's colour is laid over the picture and the scene
inside each body stays as it is. Here the bodies are drawn into the custom depth and stencil instead
(Source/OpenSCCT/Demo/ScctDemoXRay.cpp): the custom depth pass is not hidden by the walls, so the stencil holds every
body's whole silhouette. Stencil 16 marks a spy-team body, 17 a merc-team body. A pixel inside a silhouette keeps the
scene; a pixel within two pixels of one (at 1080 lines; more on a taller picture) takes that body's colour at 0.96.

Scene inputs the shader reads: 14 the picture so far, 25 the custom stencil.
"""
import unreal

PACKAGE = "/Game/OpenSCCT/Demo"
ASSET = "M_ScctDemoXRay"

# The 12 neighbours of svm_demo_xray.gdshader, in its order (the first marked one gives the colour).
XRAY = """
float2 SUV = GetDefaultSceneTextureUV(Parameters, 25);
float2 SPx = GetSceneTextureBufferSize(25).zw;
float3 C = Scene.rgb;
float S0 = round(SceneTextureLookup(SUV, 25, false).r);
if (S0 > 15.5 && S0 < 17.5)
{
    return C;
}
float2 Offsets[12] = {
    float2(-2, 0), float2(2, 0), float2(0, -2), float2(0, 2),
    float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1),
    float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1) };
float Grow = max(1.0, View.ViewSizeAndInvSize.y / 1080.0);
float Edge = 0.0;
float3 Colour = float3(0.0, 0.0, 0.0);
for (int i = 0; i < 12; i++)
{
    float Sk = round(SceneTextureLookup(SUV + Offsets[i] * SPx * Grow, 25, false).r);
    if (Edge < 0.5 && Sk > 15.5 && Sk < 17.5)
    {
        Edge = 1.0;
        Colour = Sk > 16.5 ? MercColour : SpyColour;
    }
}
return lerp(C, Colour, Edge * 0.96 * saturate(Amount));
"""


def log(text):
    unreal.log_warning("SCCT-XRAY " + text)


def build():
    path = "%s/%s" % (PACKAGE, ASSET)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    library = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # Over the finished picture, as Godot lays its contour layer over the frame (canvas colours, display encoded).
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property("blendable_priority", 5)

    def scene_texture(texture_id, y):
        node = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, y)
        node.set_editor_property("scene_texture_id", texture_id)
        return node

    inputs = [
        ("Scene", scene_texture(unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, 0), "Color"),
        ("CustomStencil", scene_texture(unreal.SceneTextureId.PPI_CUSTOM_STENCIL, 200), "Color"),
    ]
    amount = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 400)
    amount.set_editor_property("parameter_name", "Amount")
    amount.set_editor_property("default_value", 1.0)
    inputs.append(("Amount", amount, ""))
    # HudK.SpyColour and HudK.MercColour of the Godot client; the game sets them again from its own HUD colours.
    for index, (parameter, colour) in enumerate([("SpyColour", (200.0 / 255.0, 247.0 / 255.0, 184.0 / 255.0)),
                                                  ("MercColour", (1.0, 182.0 / 255.0, 68.0 / 255.0))]):
        node = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 550 + index * 200)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        inputs.append((parameter, node, ""))

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 200)
    custom.set_editor_property("description", "ScctDemoXRay")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for pin_name, _, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", XRAY.strip())

    for pin_name, node, output in inputs:
        if not library.connect_material_expressions(node, output, custom, pin_name):
            log("could not connect %s" % pin_name)
    library.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    log("built %s" % path)


build()
log("done")
