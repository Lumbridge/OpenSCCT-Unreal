"""Builds the post-process materials of the goggles: night vision, thermal vision, motion tracking, EMF vision.

Runs inside the editor's Python (Tools/Materials/Build-VisionMaterials.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_vision_materials.py"

Writes /Game/OpenSCCT/UI/Vision/M_ScctVision<Mode>. Each material is one Custom node over the finished picture
(after tonemapping), blended in by its Amount parameter (0 plain sight .. 1 the mode), which
Source/OpenSCCT/Hud/ScctVisionSubsystem drives. The shader text below is the whole look: edit it here and run the
script again; the assets are rebuilt from nothing each time.

Scene inputs the shaders read:
  14 the picture so far, 1 scene depth (cm), 13 custom depth (cm), 25 custom stencil.
The game marks characters in the custom stencil while a mode is on: in motion tracking 1 a spy, 2 a merc; in thermal
and EMF 64 to 127, the body's heat or EMF glow and its opacity in that mode (see PAWNS below). Detector beams are marked
while thermal or EMF is on by a cylinder drawn only into the custom depth (Source/OpenSCCT/Play/ScctMarkersSubsystem):
3 a beam EMF draws as a dark grey streak, 128 to 255 a beam thermal draws at the heat (stencil - 128) / 127.
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
float S0 = SceneTextureLookup(SUV, 25, false).r;
float Body = (S0 > 0.5 && S0 < 2.5
    && SceneTextureLookup(SUV, 13, false).r < SceneTextureLookup(SUV, 1, false).r + 30.0) ? 1.0 : 0.0;
float Ring = 0.0;
for (int k = 0; k < 8; k++)
{
    float Ak = k * 0.785398;
    float2 O = SUV + float2(cos(Ak), sin(Ak)) * SPx * Radius;
    float Sk = SceneTextureLookup(O, 25, false).r;
    Ring += (Sk > 0.5 && Sk < 2.5
        && SceneTextureLookup(O, 13, false).r < SceneTextureLookup(O, 1, false).r + 30.0) ? 0.125 : 0.0;
}
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

// The tube amplifies: darks are lifted hard, brights run into saturation and glow. Like a real tube it has an
// automatic gain: the whole picture's brightness (twelve spots spread over it) turns the gain down, so a lit
// room or a street under lamps is a readable green picture and not a white screen, and a dark cellar still
// gets the full lift.
float Mean = 0.0;
for (int j = 0; j < 12; j++)
{
    float2 Spot = float2((j % 4) * 0.25 + 0.125, (j / 4) * 0.3333 + 0.1667);
    Mean += dot(SceneTextureLookup(ViewportUVToSceneTextureUV(Spot, 14), 14, true).rgb, Lw);
}
Mean /= 12.0;
float Gain = clamp(0.55 / max(Mean, 0.02), 0.9, 4.5);
float A = 1.0 - exp(-(L * Gain + B * Gain * 0.45));
A += max(B * Gain / 4.5 - 0.32, 0.0) * 0.9;
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


for mode_name, mode_code in MODES:
    build(mode_name, mode_code)
build_double_vision()
log("done: %d materials" % (len(MODES) + 1))
