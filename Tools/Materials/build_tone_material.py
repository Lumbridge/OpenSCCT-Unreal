"""Builds the lighting plan's tone material: the picture's transfer under scct.Light.Plan 1.

Runs inside the editor's Python (Tools/Materials/Build-ToneMaterial.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_tone_material.py"

Writes /Game/OpenSCCT/Materials/M_ScctTone: one Custom node over the scene's light before bloom and before the
engine's tone pass (after the temporal upscale, so it works on the resolved picture, in floating point). The game
(Source/OpenSCCT/Play/ScctTone.cpp) adds it to its exposure volume while the plan is on and switches the engine's own
film curve off there, so this shader is the whole transfer from scene light to display value:

  1. The original's display value of a surface: (scene light x exposure) ^ (1 / 2.2), 0 to 2 (the original's light
     term caps at 2 and its frame buffer clips at 1).
  2. A correction the ramp solve can ask for (Tools/Light/solve_tone.py): Gain x value ^ Gamma, both 1 when the
     measured ramp agrees with the wanted one.
  3. The shoulder: values up to Knee are kept exactly (the identity the plan asks for up to 200 of 255); above, the
     overbright range the original clipped is rolled off towards 1, with the slope 1 at the knee.
  4. The original's glow gate (its normal composite, ps_1_1 0x10c4ea78): frame x saturate(16 (L - 0.03)) with
     L = dot(frame, (0.311, 0.524, 0.165)), which draws near-black as black. Gate blends it in (1 the original's, 0
     none: the night vision reads the scene before the composite, so the game takes the gate out while goggles are on).
  5. Back to the light the engine's tone pass must get so its sRGB encoding shows exactly that display value (the
     pass, with the film curve off, is light x exposure, clipped at 1, sRGB encoded), divided by the exposure the
     pass multiplies by.

A before-tone material reads and writes scene light without the engine's pre-exposure (the engine takes it out of
the lookup and puts it back on the result), so Exposure is the game's fixed exposure scale and nothing else.
"""
import unreal

PACKAGE = "/Game/OpenSCCT/Materials"
NAME = "M_ScctTone"

# (name, default). The game sets every one of them each frame; the defaults are the plan's.
SCALARS = [
    ("Exposure", 1.0),
    ("Gain", 1.0),
    ("Gamma", 1.0),
    ("Knee", 200.0 / 255.0),
    ("Gate", 1.0),
    ("GateSlope", 16.0),
    ("GateFloor", 0.03),
]

TONE = """
float E = max(Exposure, 0.0001);
float3 C = pow(max(Scene.rgb, 0.0) * E, 1.0 / 2.2);
C = Gain * pow(C, Gamma);

float K = clamp(Knee, 0.05, 0.999);
float W = 1.0 - K;
float3 Over = max(C - K, 0.0);
C = min(C, K) + W * (1.0 - exp(-Over / W));

float L = dot(C, float3(0.311, 0.524, 0.165));
C *= lerp(1.0, saturate(GateSlope * (L - GateFloor)), saturate(Gate));

float3 Low = C / 12.92;
float3 High = pow((C + 0.055) / 1.055, 2.4);
float3 Light = float3(C.r <= 0.04045 ? Low.r : High.r, C.g <= 0.04045 ? Low.g : High.g, C.b <= 0.04045 ? Low.b : High.b);
return Light / E;
"""


def log(text):
    unreal.log_warning("SCCT-TONE " + text)


def build():
    path = "%s/%s" % (PACKAGE, NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    library = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_BLOOM)

    scene = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, 0)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    inputs = [("Scene", scene, "Color")]
    for index, (parameter, default) in enumerate(SCALARS):
        node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 200 + index * 150)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", default)
        inputs.append((parameter, node, ""))

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 200)
    custom.set_editor_property("description", "ScctTone")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for pin_name, _, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", TONE.strip())

    for pin_name, node, output in inputs:
        if not library.connect_material_expressions(node, output, custom, pin_name):
            log("could not connect %s of %s" % (pin_name, NAME))
    library.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    log("built %s" % path)


build()
log("done")
