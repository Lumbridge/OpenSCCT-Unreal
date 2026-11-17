"""Builds the post-process materials of the goggles: night vision, thermal vision, motion tracking, EMF vision.

Runs inside the editor's Python (Tools/Materials/Build-VisionMaterials.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_vision_materials.py"

Writes /Game/OpenSCCT/UI/Vision/M_ScctVision<Mode>. Each material is one Custom node over the finished picture
(after tonemapping), blended in by its Amount parameter (0 plain sight .. 1 the mode), which
Source/OpenSCCT/Hud/ScctVisionSubsystem drives. The shader text below is the whole look: edit it here and run the
script again; the assets are rebuilt from nothing each time.

Scene inputs the shaders read:
  14 the picture so far, 1 scene depth (cm), 13 custom depth (cm), 25 custom stencil.
The game marks characters in the custom stencil while a mode is on: 1 a spy, 2 a merc.
"""
import unreal

PACKAGE = "/Game/OpenSCCT/UI/Vision"

# Shared opening lines of every shader.
PRELUDE = """
float2 UV = GetDefaultSceneTextureUV(Parameters, 14);
float2 Px = GetSceneTextureBufferSize(14).zw;
float2 VUV = GetViewportUV(Parameters);
float2 Res = View.ViewSizeAndInvSize.xy;
float T = View.RealTime;
float3 Lw = float3(0.30, 0.59, 0.11);
float3 C = Scene.rgb;
float L = dot(C, Lw);
float2 Q = (VUV - 0.5) * float2(Res.x / Res.y, 1.0);
float2 Cell = floor(VUV * Res / 1.5);
float Grain = frac(sin(dot(Cell + frac(T) * 37.0, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
"""

# Whether the pixel is a character that is really in view: marked in the stencil and not behind the scene.
# Also the same test averaged over a ring, for edges and halos. Uses Radius (pixels) defined by the caller.
BODY = """
float2 SUV = GetDefaultSceneTextureUV(Parameters, 25);
float2 SPx = GetSceneTextureBufferSize(25).zw;
float Body = (SceneTextureLookup(SUV, 25, false).r > 0.5
    && SceneTextureLookup(SUV, 13, false).r < SceneTextureLookup(SUV, 1, false).r + 30.0) ? 1.0 : 0.0;
float Ring = 0.0;
for (int k = 0; k < 8; k++)
{
    float Ak = k * 0.785398;
    float2 O = SUV + float2(cos(Ak), sin(Ak)) * SPx * Radius;
    Ring += (SceneTextureLookup(O, 25, false).r > 0.5
        && SceneTextureLookup(O, 13, false).r < SceneTextureLookup(O, 1, false).r + 30.0) ? 0.125 : 0.0;
}
"""

NIGHT = PRELUDE + """
// Bloom: brightness gathered from two rings around the pixel.
float B = 0.0;
for (int i = 0; i < 8; i++)
{
    float Ai = i * 0.785398 + 0.39;
    float2 D = float2(cos(Ai), sin(Ai));
    B += dot(SceneTextureLookup(UV + D * Px * 5.0, 14, true).rgb, Lw);
    B += dot(SceneTextureLookup(UV + D * Px * 13.0, 14, true).rgb, Lw) * 0.6;
}
B /= 12.8;

// The tube amplifies: darks are lifted hard, brights run into saturation and glow.
float A = 1.0 - exp(-(L * 4.5 + B * 2.2));
A += max(B - 0.32, 0.0) * 0.9;
A += Grain * 0.12;
A *= 0.95 + 0.05 * sin(VUV.y * Res.y * 1.5708);
A = saturate(A);

// Green phosphor: near black, through green, to a pale hot green.
float3 Out = lerp(float3(0.0, 0.035, 0.012), float3(0.13, 0.80, 0.30), saturate(A * 1.5));
Out = lerp(Out, float3(0.80, 1.0, 0.84), saturate((A - 0.62) * 2.6));

// The goggle's tube darkens towards the edge of the picture.
Out *= lerp(0.10, 1.0, smoothstep(1.02, 0.42, length(Q)));
return lerp(C, Out, Amount);
"""

THERMAL = PRELUDE + """
float Radius = 7.0;
""" + BODY + """
// The room as temperature rather than detail: a softened brightness, kept cold. Lamps read warmer.
float Lb = L;
for (int i = 0; i < 4; i++)
{
    float Ai = i * 1.570796 + 0.6;
    Lb += dot(SceneTextureLookup(UV + float2(cos(Ai), sin(Ai)) * Px * 4.0, 14, true).rgb, Lw);
}
Lb *= 0.2;
float Heat = 0.13 + pow(Lb, 0.7) * 0.50 + smoothstep(0.80, 1.0, Lb) * 0.20;

// Characters are hot: hottest in the middle of the body and cooler towards its outline, so the figure keeps its
// form, with warm air around it.
float Core = 0.58 + 0.30 * Ring * Ring + 0.12 * saturate(L * 3.0);
Heat = lerp(max(Heat, Ring * 0.50), Core, Body);
Heat = saturate(Heat + Grain * 0.035);

// Palette: deep blue, violet, magenta, orange, yellow, white.
float3 Out = float3(0.01, 0.01, 0.10);
Out = lerp(Out, float3(0.10, 0.05, 0.42), saturate(Heat * 5.0));
Out = lerp(Out, float3(0.62, 0.08, 0.45), saturate(Heat * 5.0 - 1.0));
Out = lerp(Out, float3(0.96, 0.36, 0.08), saturate(Heat * 5.0 - 2.0));
Out = lerp(Out, float3(1.00, 0.86, 0.25), saturate(Heat * 5.0 - 3.0));
Out = lerp(Out, float3(1.00, 1.00, 0.92), saturate(Heat * 5.0 - 4.0));
Out *= lerp(0.35, 1.0, smoothstep(1.05, 0.45, length(Q)));
return lerp(C, Out, Amount);
"""

MOTION = PRELUDE + """
float Radius = 2.5;
""" + BODY + """
// A cold steel picture with the geometry's edges picked out from depth, like a scanner's reconstruction.
float2 DUV = GetDefaultSceneTextureUV(Parameters, 1);
float2 DPx = GetSceneTextureBufferSize(1).zw;
float Z = SceneTextureLookup(DUV, 1, false).r;
float Zs = SceneTextureLookup(DUV + float2(DPx.x, 0.0) * 1.5, 1, false).r + SceneTextureLookup(DUV - float2(DPx.x, 0.0) * 1.5, 1, false).r
    + SceneTextureLookup(DUV + float2(0.0, DPx.y) * 1.5, 1, false).r + SceneTextureLookup(DUV - float2(0.0, DPx.y) * 1.5, 1, false).r;
float Edge = saturate(abs(Zs - 4.0 * Z) / (Z * 0.04 + 2.0) - 0.15) * saturate(1.0 - Z / 6000.0);

float3 Out = lerp(float3(0.015, 0.022, 0.03), float3(0.52, 0.60, 0.66), saturate(pow(L, 0.85) * 1.15));
Out += Edge * float3(0.20, 0.30, 0.36);

// Characters in view: an amber outline that pulses, and a faint fill.
float3 Amber = float3(1.0, 0.62, 0.18);
float Outline = saturate(abs(Ring - Body) * 2.2);
float Pulse = 0.75 + 0.25 * sin(T * 6.0);
Out = lerp(Out, Amber * (0.25 + 0.5 * L), Body * 0.35);
Out += Amber * Outline * Pulse;

// The sweep: a line that runs down the picture every two seconds.
float Sweep = smoothstep(0.014, 0.0, abs(frac(T * 0.5) - VUV.y));
Out += float3(0.30, 0.42, 0.50) * Sweep * 0.5;
Out += Grain * 0.03;
Out *= lerp(0.45, 1.0, smoothstep(1.05, 0.45, length(Q)));
return lerp(C, Out, Amount);
"""

EMF = PRELUDE + """
float Radius = 2.5;
""" + BODY + """
// Electrical things glow: what is bright in the picture (lamps, screens) blooms blue-white over a dark field.
float Glow = 0.0;
for (int i = 0; i < 8; i++)
{
    float Ai = i * 0.785398 + 0.2;
    float2 D = float2(cos(Ai), sin(Ai));
    Glow += smoothstep(0.55, 1.0, dot(SceneTextureLookup(UV + D * Px * 7.0, 14, true).rgb, Lw));
    Glow += smoothstep(0.55, 1.0, dot(SceneTextureLookup(UV + D * Px * 16.0, 14, true).rgb, Lw)) * 0.5;
}
Glow /= 12.0;
float Hot = smoothstep(0.55, 0.95, L);

float3 Out = lerp(float3(0.008, 0.012, 0.045), float3(0.09, 0.15, 0.40), saturate(L * 0.9));
Out += float3(0.55, 0.78, 1.0) * (Hot * 0.9 + Glow * 0.9);

// People carry little that shows: only a thin cold outline.
Out += float3(0.25, 0.45, 0.9) * saturate(abs(Ring - Body) * 2.0) * 0.55;

// Interference: slow bands drifting up the picture, and grain.
Out *= 0.90 + 0.10 * sin(VUV.y * 80.0 + T * 4.0);
Out += Grain * 0.05;
Out *= lerp(0.35, 1.0, smoothstep(1.05, 0.45, length(Q)));
return lerp(C, Out, Amount);
"""

MODES = [("Night", NIGHT), ("Thermal", THERMAL), ("Motion", MOTION), ("Emf", EMF)]


def log(text):
    unreal.log_warning("SCCT-VISION " + text)


def scene_texture(material, texture_id, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, y)
    node.set_editor_property("scene_texture_id", texture_id)
    return node


def build(name, code):
    asset_name = "M_ScctVision" + name
    path = "%s/%s" % (PACKAGE, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    library = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)

    # One node per scene input the shader reads, wired into the Custom node: that is what tells the material
    # compiler the inputs are in use, so the lookups in the shader text find them bound.
    inputs = [
        ("Scene", scene_texture(material, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, 0), "Color"),
        ("SceneDepth", scene_texture(material, unreal.SceneTextureId.PPI_SCENE_DEPTH, 200), "Color"),
        ("CustomDepth", scene_texture(material, unreal.SceneTextureId.PPI_CUSTOM_DEPTH, 400), "Color"),
        ("CustomStencil", scene_texture(material, unreal.SceneTextureId.PPI_CUSTOM_STENCIL, 600), "Color"),
    ]
    amount = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 800)
    amount.set_editor_property("parameter_name", "Amount")
    amount.set_editor_property("default_value", 1.0)
    inputs.append(("Amount", amount, ""))

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 300)
    custom.set_editor_property("description", "ScctVision" + name)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for pin_name, _, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", code.strip())

    for pin_name, node, output in inputs:
        if not library.connect_material_expressions(node, output, custom, pin_name):
            log("could not connect %s of %s" % (pin_name, asset_name))
    library.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    log("built %s" % path)


for mode_name, mode_code in MODES:
    build(mode_name, mode_code)
log("done: %d materials" % len(MODES))
