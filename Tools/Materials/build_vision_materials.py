"""Builds the post-process materials of the goggles: night vision, thermal vision, motion tracking, EMF vision.

Runs inside the editor's Python (Tools/Materials/Build-VisionMaterials.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_vision_materials.py"

Writes /Game/OpenSCCT/UI/Vision/M_ScctVision<Mode>. Each material is one Custom node over the finished picture
(after tonemapping), blended in by its Amount parameter (0 plain sight .. 1 the mode), which
Source/OpenSCCT/Hud/ScctVisionSubsystem drives (motion tracking also has the level's ramp and the tracker's clean
rectangles, see MOTION). The shader text below is the whole look: edit it here and run the script again; the assets
are rebuilt from nothing each time.

Scene inputs the shaders read:
  14 the picture so far, 1 scene depth (cm), 13 custom depth (cm), 25 custom stencil.
The game marks characters in the custom stencil while thermal or EMF is on: 64 to 127, the body's heat or EMF glow and
its opacity in that mode (see PAWNS below). Detector beams are marked
while thermal or EMF is on by a cylinder drawn only into the custom depth (Source/OpenSCCT/Play/ScctMarkersSubsystem):
3 a beam EMF draws as a dark grey streak, 128 to 255 a beam thermal draws at the heat (stencil - 128) / 127.
"""
import sys

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

# The pawn bodies of thermal and EMF, marked 64 to 127 by the simulation's rule for the mode
# (UScctVisionSubsystem::PawnStencil): code = stencil - 64, level (the mode's intensity) = floor(code / 4) / 15,
# opacity = (code % 4 + 1) / 4 (1 opaque). A body the mode hides is not drawn at all, so it is simply not here. A
# see-through body is drawn only into the custom depth, so the scene under it is what lies behind it and the body is
# laid over that by its opacity. Body: how much of the pixel is a body (its opacity), BodyLevel its level; Ring the
# bodies around the pixel (for the figure's shape), RingLevel the same weighted by their level and opacity (for halos).
PAWNS = """
float2 SUV = GetDefaultSceneTextureUV(Parameters, 25);
float2 SPx = GetSceneTextureBufferSize(25).zw;
float S0 = SceneTextureLookup(SUV, 25, false).r;
float Body = 0.0;
float BodyLevel = 0.0;
if (S0 > 63.5 && S0 < 127.5 && SceneTextureLookup(SUV, 13, false).r < SceneTextureLookup(SUV, 1, false).r + 30.0)
{
    float Code = round(S0) - 64.0;
    BodyLevel = floor(Code / 4.0) / 15.0;
    Body = (fmod(Code, 4.0) + 1.0) / 4.0;
}
float Ring = 0.0;
float RingLevel = 0.0;
for (int k = 0; k < 8; k++)
{
    float Ak = k * 0.785398;
    float2 O = SUV + float2(cos(Ak), sin(Ak)) * SPx * Radius;
    float Sk = SceneTextureLookup(O, 25, false).r;
    if (Sk > 63.5 && Sk < 127.5 && SceneTextureLookup(O, 13, false).r < SceneTextureLookup(O, 1, false).r + 30.0)
    {
        float Ck = round(Sk) - 64.0;
        float Ok = (fmod(Ck, 4.0) + 1.0) / 4.0;
        Ring += 0.125;
        RingLevel += 0.125 * Ok * floor(Ck / 4.0) / 15.0;
    }
}
"""

# The detector beams the goggles show (after BODY): Beam how much of the pixel a heat-marked beam covers and BeamHeat
# its heat, Streak how much an EMF-marked beam covers. The pixel and its four neighbours one pixel away are read,
# the neighbours at half weight, so a beam a pixel or two across reads as a soft line rather than broken dots. A
# beam is seen only where nothing of the scene stands in front of it (the beam is not in the scene depth itself).
BEAMS = """
float Beam = 0.0;
float BeamHeat = 0.0;
float Streak = 0.0;
for (int b = 0; b < 5; b++)
{
    float2 O = SUV + (b == 0 ? float2(0.0, 0.0) : float2(cos(b * 1.570796), sin(b * 1.570796)) * SPx);
    float Sb = SceneTextureLookup(O, 25, false).r;
    float Wb = (b == 0 ? 1.0 : 0.5) * (SceneTextureLookup(O, 13, false).r < SceneTextureLookup(O, 1, false).r + 5.0 ? 1.0 : 0.0);
    if (Sb > 127.5)
    {
        BeamHeat = max(BeamHeat, (Sb - 128.0) / 127.0);
        Beam = max(Beam, Wb);
    }
    if (Sb > 2.5 && Sb < 3.5)
    {
        Streak = max(Streak, Wb);
    }
}
"""

NIGHT = """
// Retail rotates and translates Engine.Noise0_gauss each frame, one bilinear texel per output pixel.
float2 Pixel = GetViewportUV(Parameters) * View.ViewSizeAndInvSize.xy;
float3 Frame = float3(Rand3DPCG16(int3(View.StateFrameIndex, 0, 0))) / 65536.0;
float2 GrainUV = Pixel / 128.0;
float S, K;
sincos(Frame.x, S, K);
GrainUV = float2(K * GrainUV.x - S * GrainUV.y, S * GrainUV.x + K * GrainUV.y) + Frame.yz;
float N = Texture2DSampleLevel(NoiseMap, NoiseMapSampler, GrainUV, 0).r;
if (HasNoise < 0.5)
{
    // Older content can lack the texture. Approximate its measured mean (0.5) and deviation (0.086).
    float3 Random = float3(Rand3DPCG16(int3(floor(Pixel), View.StateFrameIndex))) / 65536.0;
    N = 0.5 + 0.172 * dot(Random - 0.5, float3(1, 1, 1));
}
// The scene already contains the surface light boost and the engine's filtered highlight bloom.
// The original post pass (mad_x2, mul_x2) multiplies display luminance by green phosphor and grain.
float L = dot(Scene.rgb, float3(0.30, 0.59, 0.11));
float3 Night = saturate(GainIn * N * L * float3(0.718, 1.0, 0.667));
return lerp(Scene.rgb, Night, Amount);
"""

THERMAL = PRELUDE + """
float Radius = 10.0;
""" + PAWNS + BEAMS + """
// The room as temperature rather than detail: a softened brightness, kept cold. Lamps read warmer. (The original drew
// each surface from its own heat texture; those were not imported, so the room's heat comes from its brightness.)
float Lb = L;
for (int i = 0; i < 4; i++)
{
    float Ai = i * 1.570796 + 0.6;
    Lb += dot(SceneTextureLookup(UV + float2(cos(Ai), sin(Ai)) * Px * 4.0, 14, true).rgb, Lw);
}
Lb *= 0.2;
float Heat = 0.13 + pow(Lb, 0.7) * 0.50 + smoothstep(0.80, 1.0, Lb) * 0.20;

// Characters are as hot as the simulation says (HeatIntensity, 1 for both sides): hottest in the middle of the body
// and cooler towards its outline, so the figure keeps its form, with warm air around it. A body thinned by its
// HeatOpacity (the camo suit) is laid over what is behind it by that opacity; at 0 it is not marked at all.
float Core = (0.56 + 0.26 * Ring * Ring + 0.18 * saturate(L * 6.0)) * BodyLevel;
Heat = lerp(max(Heat, RingLevel * 0.50), Core, Body);

// A detector beam is drawn at its own heat (HeatIntensity: 1 a tripwire, 0.4 a mine's beam).
Heat = lerp(Heat, max(Heat, BeamHeat), Beam);
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

# Motion tracking, as the original's motion pass (ps_1_1 0x34e278) and its clean view rectangles (ps 0x10c4e328) draw
# it; the Godot client's svm_display_post.glsl mode 37 is the same decode. Everything is the blurred scene as a false
# colour ramp: lum = RampSlope x L(blurred scene x grain) + RampOffset, RampSlope = 2 (1 + 3 MTVSlope / 255) and RampOffset =
# MTVOffset / 255 from the level (set by Source/OpenSCCT/Hud/ScctVisionSubsystem), navy (0, 20, 40) to red
# (180, 10, 10) over lum x 1.7347, and above 0.5765 the fold (0.9, 0.05, 0.05) x (1 - lum); times the sonar backdrop.
# Inside each of the motion tracker's clean view rectangles (Rect0..Rect7: left, top, right, bottom in viewport UV,
# CleanCount of them in use) the sharp scene instead, in pale green monochrome: 2 x (2 x (L tint + bias)) x grain x
# sonar, each stage saturated. Nothing marks the pawns: a moving one is seen because the tracker locks it and its
# rectangle is drawn clean; one standing still is just part of the blurred ramp.
#  - The blur stands for the original's 128 x 128 level of its blur pyramid: a 7 x 7 Gaussian of taps over about
#    three of that level's texels each way.
#  - The grain stands for Engine.Noise0_gauss (mean 0.5, spread 0.085): a hash per pixel scaled to that spread.
#  - The sonar backdrop is Engine.sonar_background (8 x 16 texels, 0.8 or 1) tiled 80 x 60 over the picture, its
#    texels written out below as a bit per 0.8 texel, row by row, and read with a bilinear filter.
MOTION = PRELUDE + """
float N = 0.5 + Grain * 0.294;

// The blurred scene's luminance.
float2 BlurStep = float2(1.0 / 128.0, 1.0 / 128.0) * 0.5;
float Blurred = 0.0;
float BlurWeight = 0.0;
for (int by = -3; by <= 3; by++)
{
    for (int bx = -3; bx <= 3; bx++)
    {
        float W = exp(-0.5 * (bx * bx + by * by) / 4.0);
        float2 At = saturate(VUV + float2(bx, by) * BlurStep);
        Blurred += W * dot(SceneTextureLookup(ViewportUVToSceneTextureUV(At, 14), 14, true).rgb, Lw);
        BlurWeight += W;
    }
}
Blurred /= BlurWeight;

// The sonar backdrop.
const uint SonarRows[16] = { 227u, 193u, 128u, 128u, 128u, 128u, 193u, 227u, 62u, 28u, 8u, 8u, 8u, 8u, 28u, 62u };
float2 St = float2(frac(VUV.x * 80.0) * 8.0, frac(VUV.y * 60.0) * 16.0) - 0.5;
float2 S0 = floor(St);
float2 Sf = St - S0;
float Sonar = 0.0;
for (int c = 0; c < 4; c++)
{
    int Sx = ((int)S0.x + (c & 1) + 8) % 8;
    int Sy = ((int)S0.y + (c >> 1) + 16) % 16;
    float Texel = ((SonarRows[Sy] >> (uint)Sx) & 1u) != 0u ? 0.8 : 1.0;
    Sonar += Texel * ((c & 1) != 0 ? Sf.x : 1.0 - Sf.x) * ((c >> 1) != 0 ? Sf.y : 1.0 - Sf.y);
}

// The ramp and its fold.
float Lum = clamp(RampSlope * Blurred * N + RampOffset, -1.0, 1.0);
float3 Ramp = lerp(float3(0.0, 20.0, 40.0) / 255.0, float3(180.0, 10.0, 10.0) / 255.0, saturate(1.7347 * Lum));
float3 Fold = float3(0.9, 0.05, 0.05) * (1.0 - Lum);
float3 Out = saturate((Lum > 0.5765 ? Fold : Ramp) * Sonar);

// The clean view rectangles of the tracker's held locks.
float4 Rects[8] = { Rect0, Rect1, Rect2, Rect3, Rect4, Rect5, Rect6, Rect7 };
int Count = (int)round(CleanCount);
for (int r = 0; r < 8; r++)
{
    if (r < Count && VUV.x >= Rects[r].x && VUV.x < Rects[r].z && VUV.y >= Rects[r].y && VUV.y < Rects[r].w)
    {
        float3 M = saturate(2.0 * (L * float3(0.675, 0.75, 0.675) + float3(0.09, 0.1, 0.09)));
        M = saturate(2.0 * M * N);
        Out = saturate(M * Sonar);
        break;
    }
}
return lerp(C, Out, Amount);
"""

# The parameters a mode's material has besides Amount: (name, default) scalars and vector names.
MOTION_SCALARS = [("RampSlope", 2.0 * (1.0 + 3.0 * 85.0 / 255.0)), ("RampOffset", 52.0 / 255.0), ("CleanCount", 0.0)]
MOTION_VECTORS = ["Rect%d" % index for index in range(8)]

EMF = PRELUDE + """
float Radius = 3.5;
""" + PAWNS + BEAMS + """
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

// A pawn is here only while the simulation says EMF shows him (a spy with his weapon away is not marked and not
// drawn at all). He is a dim figure in the field, glowing by his EMF glow (EMFIntensity while bEMFIsOn) and, on what
// he carries, by its glow (the drawn pistol), with a halo of it around him; laid over the scene by his EMFOpacity.
float3 Figure = lerp(float3(0.03, 0.05, 0.14), float3(0.12, 0.20, 0.48), saturate(L * 0.9));
Figure += float3(0.55, 0.78, 1.0) * BodyLevel * (1.2 + 0.25 * sin(T * 5.0));
Out = lerp(Out, Figure, Body);
Out += float3(0.45, 0.65, 1.0) * RingLevel * (1.0 - saturate(Body * 4.0)) * 0.8;

// A detector beam is a dark grey streak: it gives off nothing, the field only shows it is there.
Out = lerp(Out, float3(0.24, 0.24, 0.25), Streak);

// Interference: slow bands drifting up the picture, and grain.
Out *= 0.90 + 0.10 * sin(VUV.y * 80.0 + T * 4.0);
Out += Grain * 0.05;
Out *= lerp(0.35, 1.0, smoothstep(1.05, 0.45, length(Q)));
return lerp(C, Out, Amount);
"""

MODES = [("Night", NIGHT, [("HasNoise", 0)], []), ("Thermal", THERMAL, [], []), ("Motion", MOTION, MOTION_SCALARS, MOTION_VECTORS), ("Emf", EMF, [], [])]


def log(text):
    unreal.log_warning("SCCT-VISION " + text)


def scene_texture(material, texture_id, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, y)
    node.set_editor_property("scene_texture_id", texture_id)
    return node


def build(name, code, scalars, vectors):
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
    for index, (parameter, default) in enumerate(scalars):
        node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 950 + index * 150)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", default)
        inputs.append((parameter, node, ""))
    # A vector parameter's first output is its RGB only, so each is joined with its A into a float4 for the shader.
    for index, parameter in enumerate(vectors):
        y = 950 + (len(scalars) + index) * 150
        node = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1000, y)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        append = library.create_material_expression(material, unreal.MaterialExpressionAppendVector, -700, y)
        if not library.connect_material_expressions(node, "", append, "A") or not library.connect_material_expressions(node, "A", append, "B"):
            log("could not join %s of %s" % (parameter, asset_name))
        inputs.append((parameter, append, ""))
    if "GainIn" in code:
        y = 950 + (len(scalars) + len(vectors)) * 150
        gain = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, y)
        gain.set_editor_property("parameter_name", "Gain")
        gain.set_editor_property("default_value", 4.0)
        inputs.append(("GainIn", gain, ""))
    if name == "Night":
        noise = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -700, 1400)
        noise.set_editor_property("parameter_name", "NoiseMap")
        noise.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
        inputs.append(("NoiseMap", noise, ""))

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


# Cross-eyed double vision (spec/hud/screen-effects.md, "Cross-eyed double vision"), as the original's final draw of
# the frame does it: the picture twice, half each. One copy shows U 0 to 1 - SpreadA across the screen, the other U
# SpreadB to 1, V unchanged, so each is stretched from its own side. Source/OpenSCCT/Hud/ScctScreenEffectsSubsystem
# works the two spreads out from CrossEyedFactor and the time (the original's formula) and sets them each frame;
# Amount blends the doubled picture in (1 while the factor is on).
DOUBLE = """
float2 VUV = GetViewportUV(Parameters);
float3 C = Scene.rgb;
float2 U0 = float2(VUV.x * (1.0 - SpreadA), VUV.y);
float2 U1 = float2(SpreadB + VUV.x * (1.0 - SpreadB), VUV.y);
float3 Doubled = 0.5 * (SceneTextureLookup(ViewportUVToSceneTextureUV(U0, 14), 14, true).rgb
    + SceneTextureLookup(ViewportUVToSceneTextureUV(U1, 14), 14, true).rgb);
return lerp(C, Doubled, saturate(Amount));
"""


def build_double_vision():
    asset_name = "M_ScctDoubleVision"
    path = "%s/%s" % (PACKAGE, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    library = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    # After the goggles' materials (priority 0): the original doubled the finished frame as its very last draw.
    material.set_editor_property("blendable_priority", 10)

    inputs = [("Scene", scene_texture(material, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, 0), "Color")]
    for index, (parameter, default) in enumerate([("Amount", 0.0), ("SpreadA", 0.0), ("SpreadB", 0.0)]):
        node = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 200 + index * 150)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", default)
        inputs.append((parameter, node, ""))

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 200)
    custom.set_editor_property("description", "ScctDoubleVision")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for pin_name, _, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", DOUBLE.strip())

    for pin_name, node, output in inputs:
        if not library.connect_material_expressions(node, output, custom, pin_name):
            log("could not connect %s of %s" % (pin_name, asset_name))
    library.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    log("built %s" % path)


if __name__ == "__main__":
    for mode_name, mode_code, mode_scalars, mode_vectors in MODES:
        if len(sys.argv) == 1 or mode_name in sys.argv[1:]:
            build(mode_name, mode_code, mode_scalars, mode_vectors)
    if len(sys.argv) == 1 or "DoubleVision" in sys.argv[1:]:
        build_double_vision()
