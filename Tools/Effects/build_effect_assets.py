"""Builds the materials of the effects (Source/OpenSCCT/Play/ScctEffects*) and imports the game's effect meshes.
Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="<full path>/build_effect_assets.py [<content root>] [--force]"
(forward slashes in every path: the script switch reads a backslash as an escape)

The materials go to /Game/OpenSCCT/Effects/ and are committed: they are ours, made from nothing but maths.
  M_ScctFxGlow    additive unlit sprite; colour and opacity per instance (the first four custom data floats) and
                  the distance in cm over which it dims into a surface behind it (the fifth)
  M_ScctFxSmoke   soft translucent sprite lit by the scene; colour and opacity per instance
  M_ScctFxCloud   a textured cloud sprite lit by the scene (the spy's smoke grenade): one cell of its Texture's
                  Grid per instance (the fifth custom value), times the instance's colour and opacity
  M_ScctFxDecal   deferred decal: a bullet hole with a ragged lip in a chipped ring; parameters Color, RimColor,
                  Strength and RimStrength
  M_ScctFxSolid   opaque surface with a glow, for stand-in shapes; parameters Color and Glow
  M_ScctFxProjection  deferred decal of an abandoned projection (footprint, flare burn, pellet marks); parameters
                  Texture, Tint, UseTint, FlipV and Opacity
  M_ScctFxSmokeVolume  a responsive smoke cloud, ray-marched through the density grid the bridge samples from the
                  shared smoke field (Play/ScctEffectsSmokeVolume.cpp); drawn on the back faces of a box over the
                  field's bounds, with no depth test of its own (it stops at the scene's depth itself)
  M_ScctGoggleGlowHue  the goggle lenses with their colour turned by HueTurn radians (a player's light cosmetic);
                  parameters Lens, Intensity, Fade and HueTurn as M_ScctGoggleGlow has them

With a content root the original effect meshes (grenades, mines, gadgets, beams: shared/fx/meshes/*.glb) are
imported to /Game/Imported/Fx/<Package>/ and the projections' textures to /Game/Imported/Fx/Textures/ (never
committed: original game content). An existing material is rebuilt only with --force as the last argument, or with
--force=<name>,<name> for just those.
"""
import os
import sys

import unreal

ARGS = [a for a in sys.argv[1:] if not a.startswith("--force")]
FORCE = "--force" in sys.argv
# --force=M_A,M_B rebuilds only those materials.
FORCE_ONLY = set(n for a in sys.argv[1:] if a.startswith("--force=") for n in a[len("--force="):].split(",") if n)
CONTENT_ROOT = ARGS[0] if ARGS else ""
FOLDER = "/Game/OpenSCCT/Effects"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def try_set(target, name, value):
    try:
        target.set_editor_property(name, value)
    except Exception as error:  # pylint: disable=broad-except
        log("property not set: %s (%s)" % (name, str(error).split(":")[0]))


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
        if not FORCE and name not in FORCE_ONLY:
            return None
        editor_assets.delete_asset(path)
    return asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def finish(material):
    lib.recompile_material(material)
    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
    log("material built: " + material.get_path_name())


def in_the_picture(material, source, x, y):
    """`source` as a brightness in the finished picture instead of in the scene: divided by the exposure the eye
    has settled on, within bounds (the same rule as Graph.in_the_picture in Tools/Import/scct_materials.py). A
    flash or a beam is then as bright on screen in a dark cellar as in a lit hall, and never a white-out."""
    inverse = lib.create_material_expression(material, unreal.MaterialExpressionEyeAdaptationInverse, x, y)
    one = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x - 200, y + 120)
    one.set_editor_property("constant", unreal.LinearColor(1, 1, 1, 1))
    if not (lib.connect_material_expressions(one, "", inverse, "LightValueInput")
            or lib.connect_material_expressions(one, "", inverse, "LightValue")):
        log("NOT CONNECTED: the eye adaptation node of " + material.get_name())
    custom = lib.create_material_expression(material, unreal.MaterialExpressionCustom, x + 220, y)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in ("L", "K"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", "float s = (K.r + K.g + K.b) / 3.0; return L * clamp(s, 0.08, 16.0);")
    lib.connect_material_expressions(source, "", custom, "L")
    lib.connect_material_expressions(inverse, "", custom, "K")
    return custom


def soft_disc(material, x, y, power):
    """(1 - distance from the middle of the quad) ^ power: 1 in the middle, 0 at the rim."""
    def node(cls, dx, dy):
        return lib.create_material_expression(material, cls, x + dx, y + dy)

    uv = node(unreal.MaterialExpressionTextureCoordinate, 0, 0)
    middle = node(unreal.MaterialExpressionConstant2Vector, 0, 120)
    middle.set_editor_property("r", 0.5)
    middle.set_editor_property("g", 0.5)
    offset = node(unreal.MaterialExpressionSubtract, 200, 0)
    connect(uv, "", offset, "A")
    connect(middle, "", offset, "B")
    length = node(unreal.MaterialExpressionLength, 380, 0)
    connect(offset, "", length, "")
    twice = node(unreal.MaterialExpressionMultiply, 540, 0)
    twice.set_editor_property("const_b", 2.0)
    connect(length, "", twice, "A")
    inverse = node(unreal.MaterialExpressionOneMinus, 700, 0)
    connect(twice, "", inverse, "")
    clamped = node(unreal.MaterialExpressionSaturate, 860, 0)
    connect(inverse, "", clamped, "")
    shaped = node(unreal.MaterialExpressionPower, 1020, 0)
    shaped.set_editor_property("const_exponent", power)
    connect(clamped, "", shaped, "Base")
    return shaped


def instance_colour(material, x, y):
    colour = lib.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, x, y)
    colour.set_editor_property("data_index", 0)
    colour.set_editor_property("const_default_value", unreal.LinearColor(1, 1, 1, 1))
    alpha = lib.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, x, y + 200)
    alpha.set_editor_property("data_index", 3)
    alpha.set_editor_property("const_default_value", 1.0)
    return colour, alpha


def build_glow():
    material = new_material("M_ScctFxGlow")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    try_set(material, "used_with_instanced_static_meshes", True)

    disc = soft_disc(material, -1500, 300, 2.0)
    colour, alpha = instance_colour(material, -900, -100)
    level = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, 200)
    connect(disc, "", level, "A")
    connect(alpha, "", level, "B")
    light = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -150, 0)
    connect(colour, "", light, "A")
    connect(level, "", light, "B")
    # A fireball dims where it meets a floor or a wall instead of ending in a hard line. The distance over which
    # it does so comes with each sprite (its fifth value), because a spark a centimetre off a wall must stay
    # bright where a metre-wide fireball needs a wide, soft edge.
    reach = lib.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -350, 420)
    reach.set_editor_property("data_index", 4)
    reach.set_editor_property("const_default_value", 10.0)
    fade = lib.create_material_expression(material, unreal.MaterialExpressionDepthFade, -150, 320)
    fade.set_editor_property("fade_distance_default", 10.0)
    connect(reach, "", fade, "FadeDistance")
    soft = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, 30, 100)
    connect(light, "", soft, "A")
    connect(fade, "", soft, "B")
    connect_output(in_the_picture(material, soft, 250, 100), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


def build_smoke():
    material = new_material("M_ScctFxSmoke")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    # Lit from the scene's lighting volume, so smoke darkens in shadow and flares in a muzzle flash.
    try_set(material, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    try_set(material, "used_with_instanced_static_meshes", True)

    disc = soft_disc(material, -1500, 300, 1.5)
    colour, alpha = instance_colour(material, -900, -100)
    connect_output(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, 200)
    connect(disc, "", opacity, "A")
    connect(alpha, "", opacity, "B")
    # A puff thins out where it meets a floor or a wall instead of ending in a hard line.
    fade = lib.create_material_expression(material, unreal.MaterialExpressionDepthFade, -350, 320)
    fade.set_editor_property("fade_distance_default", 45.0)
    soft = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -180, 200)
    connect(opacity, "", soft, "A")
    connect(fade, "", soft, "B")
    connect_output(soft, "", unreal.MaterialProperty.MP_OPACITY)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 400)
    rough.set_editor_property("r", 1.0)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # A little light of its own, so dust in a dark corner is a faint haze and not a black blot.
    haze = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, -300)
    haze.set_editor_property("const_b", 0.05)
    connect(colour, "", haze, "A")
    connect_output(haze, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


def build_cloud():
    """A sprite of a textured cloud (the spy's smoke grenade: Play/ScctEffectsSubsystem.cpp, EmitGrenadeCloud): one
    cell of the Texture parameter's Grid (columns, rows), the cell picked per instance (the fifth custom value),
    times the instance's colour and opacity. The original draws it unlit; here the room lights it, with a floor of
    its own in the finished picture (Floor) so it is a grey cloud in the dark too and never a black blot."""
    material = new_material("M_ScctFxCloud")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    try_set(material, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    try_set(material, "used_with_instanced_static_meshes", True)

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    uv = node(unreal.MaterialExpressionTextureCoordinate, -1500, 0)
    cell = node(unreal.MaterialExpressionPerInstanceCustomData, -1500, 150)
    cell.set_editor_property("data_index", 4)
    cell.set_editor_property("const_default_value", 0.0)
    grid = node(unreal.MaterialExpressionVectorParameter, -1500, 300)
    grid.set_editor_property("parameter_name", "Grid")
    grid.set_editor_property("default_value", unreal.LinearColor(2, 2, 0, 0))
    texture = node(unreal.MaterialExpressionTextureObjectParameter, -1500, -200)
    texture.set_editor_property("parameter_name", "Texture")
    default = unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    if default:
        texture.set_editor_property("texture", default)

    look = node(unreal.MaterialExpressionCustom, -1100, 0)
    look.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    pins = []
    for name in ("Tex", "UV", "Cell", "Grid"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    look.set_editor_property("inputs", pins)
    # Half a texel is kept clear of the cell's edge, so a cell never shows its neighbour's rim.
    look.set_editor_property("code", "float2 g = max(Grid.xy, 1.0);\n"
                                     "float c = floor(Cell + 0.5);\n"
                                     "float2 o = float2(fmod(c, g.x), floor(c / g.x));\n"
                                     "float2 uv = (clamp(UV, 0.01, 0.99) + o) / g;\n"
                                     "return Texture2DSample(Tex, TexSampler, uv);")
    connect(texture, "", look, "Tex")
    connect(uv, "", look, "UV")
    connect(cell, "", look, "Cell")
    connect(grid, "", look, "Grid")
    rgb = node(unreal.MaterialExpressionComponentMask, -850, -50)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(channel, on)
    connect(look, "", rgb, "")
    cover = node(unreal.MaterialExpressionComponentMask, -850, 150)
    for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        cover.set_editor_property(channel, on)
    connect(look, "", cover, "")

    colour, alpha = instance_colour(material, -850, -400)
    base = node(unreal.MaterialExpressionMultiply, -600, -150)
    connect(rgb, "", base, "A")
    connect(colour, "", base, "B")
    connect_output(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    opacity = node(unreal.MaterialExpressionMultiply, -600, 150)
    connect(cover, "", opacity, "A")
    connect(alpha, "", opacity, "B")
    # A short soft edge where a sprite meets the floor or a wall; short, so somebody against a wall stays hidden.
    fade = node(unreal.MaterialExpressionDepthFade, -600, 320)
    fade.set_editor_property("fade_distance_default", 20.0)
    soft = node(unreal.MaterialExpressionMultiply, -400, 200)
    connect(opacity, "", soft, "A")
    connect(fade, "", soft, "B")
    connect_output(soft, "", unreal.MaterialProperty.MP_OPACITY)

    rough = node(unreal.MaterialExpressionConstant, -600, 450)
    rough.set_editor_property("r", 1.0)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    floor = node(unreal.MaterialExpressionScalarParameter, -600, -500)
    floor.set_editor_property("parameter_name", "Floor")
    floor.set_editor_property("default_value", 0.2)
    own = node(unreal.MaterialExpressionMultiply, -400, -400)
    connect(base, "", own, "A")
    connect(floor, "", own, "B")
    connect_output(in_the_picture(material, own, -150, -400), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


def build_decal():
    material = new_material("M_ScctFxDecal")
    if not material:
        return
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    # A bullet hole: a small dark hole with a ragged lip, in a ring of chipped surface with a broken outline, so
    # no two marks have the same shape once each is turned its own way. x: how much of the pixel is the hole
    # (the rest is the chipped ring's colour), y: how much the mark covers the surface there.
    code = """
float2 p = UV * 2.0 - 1.0;
float r = length(p);
float a = atan2(p.y, p.x);
float lip = 0.38 + 0.035 * sin(a * 3.0 + 0.7) + 0.025 * sin(a * 7.0 + 2.1);
float hole = smoothstep(lip, lip - 0.07, r);
float edge = 0.70 + 0.13 * sin(a * 2.0 + 1.3) + 0.09 * sin(a * 5.0 + 0.4) + 0.05 * sin(a * 11.0 + 2.9);
float chip = smoothstep(edge, edge - 0.22, r);
// Thin cracks run out of the ring a little way.
float crack = pow(saturate(sin(a * 4.0 + 0.9) * 0.5 + 0.5), 24.0) * smoothstep(0.98, 0.55, r) * 0.6;
return float2(hole, saturate(max(hole, max(chip, crack) * Rim)));"""
    uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1500, 300)
    rim_strength = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1500, 480)
    rim_strength.set_editor_property("parameter_name", "RimStrength")
    rim_strength.set_editor_property("default_value", 0.55)
    shape = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -1150, 300)
    shape.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    pins = []
    for name in ("UV", "Rim"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    shape.set_editor_property("inputs", pins)
    shape.set_editor_property("code", code.strip())
    connect(uv, "", shape, "UV")
    connect(rim_strength, "", shape, "Rim")
    hole_mask = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -850, 250)
    hole_mask.set_editor_property("r", True)
    hole_mask.set_editor_property("g", False)
    connect(shape, "", hole_mask, "")
    cover = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -850, 400)
    cover.set_editor_property("r", False)
    cover.set_editor_property("g", True)
    connect(shape, "", cover, "")

    hole = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -250)
    hole.set_editor_property("parameter_name", "Color")
    hole.set_editor_property("default_value", unreal.LinearColor(0.008, 0.007, 0.006, 1.0))
    rim = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -50)
    rim.set_editor_property("parameter_name", "RimColor")
    rim.set_editor_property("default_value", unreal.LinearColor(0.42, 0.38, 0.33, 1.0))
    colour = lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -450, -100)
    connect(rim, "", colour, "A")
    connect(hole, "", colour, "B")
    connect(hole_mask, "", colour, "Alpha")
    connect_output(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

    strength = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -550, 480)
    strength.set_editor_property("parameter_name", "Strength")
    strength.set_editor_property("default_value", 0.92)
    opacity = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 350)
    connect(cover, "", opacity, "A")
    connect(strength, "", opacity, "B")
    connect_output(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 600)
    rough.set_editor_property("r", 0.95)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(material)


def build_solid():
    material = new_material("M_ScctFxSolid")
    if not material:
        return
    colour = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -500, -100)
    colour.set_editor_property("parameter_name", "Color")
    colour.set_editor_property("default_value", unreal.LinearColor(0.05, 0.05, 0.05, 1.0))
    connect_output(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -500, 150)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 1.0))
    connect_output(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 400)
    rough.set_editor_property("r", 0.45)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 500)
    metal.set_editor_property("r", 0.6)
    connect_output(metal, "", unreal.MaterialProperty.MP_METALLIC)
    finish(material)


def build_projection():
    material = new_material("M_ScctFxProjection")
    if not material:
        return
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    # An abandoned projection (a footprint, a flare's burn mark, a shotgun's pellet marks): its texture, turned
    # upside down for a right foot (FlipV 1). A tinted one (UseTint 1, a footprint's Combiner) takes its colour from
    # Tint and its opacity from the texture's alpha times the tint's; any other is the texture as it is. Opacity
    # fades it out at the end of its life.
    uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1500, 0)
    flip = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1500, 150)
    flip.set_editor_property("parameter_name", "FlipV")
    flip.set_editor_property("default_value", 0.0)
    flipped = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -1250, 0)
    flipped.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    pins = []
    for name in ("UV", "Flip"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    flipped.set_editor_property("inputs", pins)
    flipped.set_editor_property("code", "return float2(UV.x, lerp(UV.y, 1.0 - UV.y, Flip));")
    connect(uv, "", flipped, "UV")
    connect(flip, "", flipped, "Flip")

    # The texture goes into the node as a texture object and is sampled there: a sampler node's alpha output,
    # wired by name from here, reached the node as its colour instead.
    texture = lib.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -1000, 0)
    texture.set_editor_property("parameter_name", "Texture")
    default = unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    if default:
        texture.set_editor_property("texture", default)

    tint = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1000, -300)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    tint_alpha = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1000, 200)
    tint_alpha.set_editor_property("parameter_name", "TintAlpha")
    tint_alpha.set_editor_property("default_value", 1.0)
    use_tint = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1000, 300)
    use_tint.set_editor_property("parameter_name", "UseTint")
    use_tint.set_editor_property("default_value", 0.0)
    fade = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1000, 420)
    fade.set_editor_property("parameter_name", "Opacity")
    fade.set_editor_property("default_value", 1.0)

    look = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -600, 0)
    look.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    pins = []
    for name in ("Tex", "UV", "Tint", "TintAlpha", "UseTint", "Fade"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    look.set_editor_property("inputs", pins)
    look.set_editor_property("code", "float4 t = Texture2DSample(Tex, TexSampler, UV);\n"
                                     "float3 c = lerp(t.rgb, Tint.rgb, UseTint);\n"
                                     "float a = t.a * lerp(1.0, TintAlpha, UseTint) * Fade;\n"
                                     "return float4(c, saturate(a));")
    connect(texture, "", look, "Tex")
    connect(flipped, "", look, "UV")
    connect(tint, "", look, "Tint")
    connect(tint_alpha, "", look, "TintAlpha")
    connect(use_tint, "", look, "UseTint")
    connect(fade, "", look, "Fade")
    colour = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -300, -50)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        colour.set_editor_property(channel, on)
    connect(look, "", colour, "")
    connect_output(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -300, 150)
    for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        opacity.set_editor_property(channel, on)
    connect(look, "", opacity, "")
    connect_output(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 350)
    rough.set_editor_property("r", 0.9)
    connect_output(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(material)


# The ray march of a responsive smoke cloud. Inputs: CamRel (the camera in the box's frame, cm), PixelRel (the pixel
# from the camera, cm), PixDepth and SceneD (the pixel's and the scene's depth along the view axis), the two density
# atlases (DensA, DensB) and Blend between them, N (grid cells per axis), Tiles (slices per atlas row), Size (the
# box's edge), Ext (optical depth per cm at density 1: ResponsiveSmokeField.Extinction, the same as gas and AI use),
# Steps and ShadowSteps (the quality), Ambient, LightCol and LightDir (the light at the cloud), Albedo, Time and Flow,
# and the shot holes: HoleTex holds HoleCount rows of (start, radius), (end, strength), (feather) in the box's frame.
# Each density atlas holds the N slices of the grid side by side, Tiles to a row; a slice is read with the texture's
# own bilinear filter and two slices are mixed by hand, so the grid reads as a smooth volume.
SMOKE_VOLUME_CODE = r"""
struct FSmokeGrid
{
    float N;
    float Tiles;
    float Size;
    float Time;
    float Flow;
    float Slices(Texture2D Tex, SamplerState Smp, float3 p)
    {
        p = saturate(p);
        float z = p.z * N - 0.5;
        float z0 = clamp(floor(z), 0.0, N - 1.0);
        float z1 = min(z0 + 1.0, N - 1.0);
        float f = saturate(z - z0);
        float2 inner = clamp(p.xy * N, 0.5, N - 0.5) / N;
        float2 tile0 = float2(fmod(z0, Tiles), floor(z0 / Tiles));
        float2 tile1 = float2(fmod(z1, Tiles), floor(z1 / Tiles));
        float a = Tex.SampleLevel(Smp, (tile0 + inner) / Tiles, 0).r;
        float b = Tex.SampleLevel(Smp, (tile1 + inner) / Tiles, 0).r;
        return lerp(a, b, f);
    }
    // The density at q (cm in the box's frame): the grid read through a slow rolling warp, broken up at a finer
    // scale. Both only move what is drawn; where the field is empty (a wall, a hole a shot left) it stays empty.
    float Density(Texture2D A, SamplerState SA, Texture2D B, SamplerState SB, float Blend, Texture2D H, int Holes, float3 q)
    {
        float3 warp = float3(sin(q.z * 0.011 + Time * 0.9), sin(q.x * 0.012 - Time * 0.7 + 1.7), sin(q.y * 0.010 + Time * 0.8 + 3.1));
        float3 p = (q + warp * (14.0 * Flow) + float3(0.0, 0.0, -6.0 * Flow * sin(Time * 0.31))) / Size;
        float d = lerp(Slices(A, SA, p), Slices(B, SB, p), Blend);
        float fine = sin(q.x * 0.043 + Time * 1.3) * sin(q.y * 0.051 - Time * 1.1) * sin(q.z * 0.037 + Time * 0.9);
        float broad = sin(q.x * 0.017 - Time * 0.5 + 0.4) * sin(q.y * 0.015 + Time * 0.6) * sin(q.z * 0.019 + Time * 0.4 + 2.2);
        d = saturate(d * (1.0 + Flow * (0.30 * fine + 0.25 * broad)));
        // The shot holes, exactly where they run (finer than the grid), at the real position as in the simulation:
        // each holds back 1 - smoothstep(r, r + feather, distance to its line) of the smoke, times how open it still is.
        for (int i = 0; i < Holes && d > 0.0; i++)
        {
            float4 s = H.Load(int3(0, i, 0));
            float4 e = H.Load(int3(1, i, 0));
            float feather = max(H.Load(int3(2, i, 0)).x, 0.01);
            float3 seg = e.xyz - s.xyz;
            float len2 = max(dot(seg, seg), 1e-4);
            float u = saturate(dot(q - s.xyz, seg) / len2);
            float dist = length(q - (s.xyz + seg * u));
            float spatial = 1.0 - smoothstep(s.w, s.w + feather, dist);
            d *= 1.0 - spatial * e.w;
        }
        return d;
    }
};
FSmokeGrid g;
g.N = max(N, 2.0); g.Tiles = max(Tiles, 1.0); g.Size = max(Size, 1.0); g.Time = Time; g.Flow = Flow;

float dist = length(PixelRel);
if (dist < 0.001) return float4(0, 0, 0, 0);
float3 dir = PixelRel / dist;
float3 d = float3(abs(dir.x) < 1e-5 ? 1e-5 : dir.x, abs(dir.y) < 1e-5 ? 1e-5 : dir.y, abs(dir.z) < 1e-5 ? 1e-5 : dir.z);
float3 t0 = -CamRel / d;
float3 t1 = (g.Size - CamRel) / d;
float3 lo = min(t0, t1);
float3 hi = max(t0, t1);
float tEnter = max(max(lo.x, lo.y), max(lo.z, 0.0));
float tExit = min(min(hi.x, hi.y), hi.z);
// Both faces of the box are drawn; the far one draws the cloud, the near one nothing (else it would count twice).
if (dist < 0.5 * (tEnter + tExit)) return float4(0, 0, 0, 0);
// The material has no depth test of its own: the march stops at the scene's opaque surface.
float perDepth = dist / max(PixDepth, 0.001);
tExit = min(tExit, SceneD * perDepth);
if (tExit <= tEnter) return float4(0, 0, 0, 0);

float stepLen = g.Size / max(Steps, 4.0);
float jitter = frac(sin(dot(Parameters.SvPosition.xy, float2(12.9898, 78.233))) * 43758.5453);
float t = tEnter + stepLen * jitter;
float T = 1.0;
float3 L = 0;
float3 lightDir = normalize(LightDir + float3(0, 0, 1e-4));
float shadowStep = 28.0;
[loop]
for (int i = 0; i < 160; i++)
{
    if (t >= tExit) break;
    float3 q = CamRel + dir * t;
    float dens = g.Density(DensA, DensASampler, DensB, DensBSampler, Blend, HoleTex, (int)HoleCount, q);
    if (dens > 0.002)
    {
        float dt = min(stepLen, tExit - t);
        float a = 1.0 - exp(-dens * Ext * dt);
        // Self-shadow: the smoke between this point and the light (fewer steps at lower quality).
        float tau = 0.0;
        int shadowSteps = dot(LightCol, float3(1, 1, 1)) > 1e-5 ? (int)ShadowSteps : 0;
        [loop]
        for (int s = 1; s <= shadowSteps; s++)
        {
            tau += g.Density(DensA, DensASampler, DensB, DensBSampler, Blend, HoleTex, 0, q + lightDir * (shadowStep * s)) * Ext * shadowStep;
        }
        float3 lit = Ambient + LightCol * exp(-tau * 0.35);
        L += T * a * lit * Albedo;
        T *= 1.0 - a;
        if (T < 0.004) break;
    }
    t += stepLen;
}
return float4(L, saturate(1.0 - T));
"""


def build_smoke_volume():
    material = new_material("M_ScctFxSmokeVolume")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    try_set(material, "disable_depth_test", True)
    try_set(material, "use_translucency_vertex_fog", False)

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    def scalar(name, value, x, y):
        p = node(unreal.MaterialExpressionScalarParameter, x, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", value)
        return p

    def vector(name, value, x, y):
        p = node(unreal.MaterialExpressionVectorParameter, x, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(value[0], value[1], value[2], 1.0))
        return p

    # The camera in the box's frame: the world's large coordinates are taken apart here, in the graph, and only the
    # small difference goes into the code.
    camera = node(unreal.MaterialExpressionCameraPositionWS, -1900, -400)
    origin = vector("BoxOrigin", (0, 0, 0), -1900, -250)
    cam_rel = node(unreal.MaterialExpressionSubtract, -1650, -350)
    connect(camera, "", cam_rel, "A")
    connect(origin, "", cam_rel, "B")
    cam_rel_float = node(unreal.MaterialExpressionTruncateLWC, -1450, -350)
    connect(cam_rel, "", cam_rel_float, "")
    pixel_rel = node(unreal.MaterialExpressionWorldPosition, -1650, -150)
    try_set(pixel_rel, "world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    pixel_depth = node(unreal.MaterialExpressionPixelDepth, -1650, -50)
    scene_depth = node(unreal.MaterialExpressionSceneDepth, -1650, 50)
    time = node(unreal.MaterialExpressionTime, -1650, 150)

    texture_a = node(unreal.MaterialExpressionTextureObjectParameter, -1650, 300)
    texture_a.set_editor_property("parameter_name", "DensityA")
    texture_b = node(unreal.MaterialExpressionTextureObjectParameter, -1650, 500)
    texture_b.set_editor_property("parameter_name", "DensityB")
    default = unreal.load_asset("/Engine/EngineResources/Black")
    if default:
        texture_a.set_editor_property("texture", default)
        texture_b.set_editor_property("texture", default)

    holes = node(unreal.MaterialExpressionTextureObjectParameter, -1650, 700)
    holes.set_editor_property("parameter_name", "Holes")
    if default:
        holes.set_editor_property("texture", default)

    inputs = [
        ("CamRel", cam_rel_float),
        ("PixelRel", pixel_rel),
        ("PixDepth", pixel_depth),
        ("SceneD", scene_depth),
        ("DensA", texture_a),
        ("DensB", texture_b),
        ("Blend", scalar("Blend", 1.0, -1300, 300)),
        ("N", scalar("GridSize", 32.0, -1300, 400)),
        ("Tiles", scalar("Tiles", 6.0, -1300, 500)),
        ("Size", scalar("BoxSize", 625.0, -1300, 600)),
        ("Ext", scalar("Extinction", 0.06, -1300, 700)),
        ("Steps", scalar("Steps", 56.0, -1300, 800)),
        ("ShadowSteps", scalar("ShadowSteps", 2.0, -1300, 900)),
        ("Ambient", vector("Ambient", (0.02, 0.02, 0.022), -1300, 1000)),
        ("LightCol", vector("LightColor", (0.1, 0.1, 0.1), -1300, 1150)),
        ("LightDir", vector("LightDirection", (0, 0, 1), -1300, 1300)),
        ("Albedo", vector("Albedo", (0.72, 0.72, 0.74), -1300, 1450)),
        ("Time", time),
        ("Flow", scalar("Flow", 1.0, -1300, 1600)),
        ("HoleTex", holes),
        ("HoleCount", scalar("HoleCount", 0.0, -1300, 1700)),
    ]
    march = node(unreal.MaterialExpressionCustom, -800, 300)
    march.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    pins = []
    for name, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    march.set_editor_property("inputs", pins)
    march.set_editor_property("code", SMOKE_VOLUME_CODE.strip())
    for name, source in inputs:
        connect(source, "", march, name)

    colour = node(unreal.MaterialExpressionComponentMask, -450, 200)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        colour.set_editor_property(channel, on)
    connect(march, "", colour, "")
    connect_output(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = node(unreal.MaterialExpressionComponentMask, -450, 400)
    for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        opacity.set_editor_property(channel, on)
    connect(march, "", opacity, "")
    connect_output(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    finish(material)


# The textures the abandoned projections draw (Bridge/OpenScct.UnrealBridge/Events/ProjectionExports.cs): every
# footprint texture, the flare's burn mark and the shotgun's pellet marks, by their object paths in
# shared/fx/effects.json. Imported as /Game/Imported/Fx/Textures/<picture name> (sfx_step01), the name the bridge
# hands with each projection.
PROJECTION_TEXTURES = ("sfx.FootPrints.", "sfx.Flare.Burn_flare", "sfx.Impacts.FX_shothun_near")


def import_projection_textures():
    import json
    folder = os.path.join(CONTENT_ROOT, "shared", "fx")
    path = os.path.join(folder, "effects.json")
    if not os.path.isfile(path):
        log("no effect table at " + path)
        return
    with open(path, "r", encoding="utf-8") as handle:
        textures = json.load(handle).get("textures", {})
    tasks = []
    pictures = set()
    for name, entry in sorted(textures.items()):
        png = entry.get("png")
        if not png or not any(name == p or (p.endswith(".") and name.startswith(p)) for p in PROJECTION_TEXTURES):
            continue
        stem = os.path.splitext(os.path.basename(png))[0]
        if stem in pictures:
            continue
        pictures.add(stem)
        task = unreal.AssetImportTask()
        task.filename = os.path.join(folder, png)
        task.destination_path = "/Game/Imported/Fx/Textures"
        task.destination_name = stem
        task.automated = True
        task.save = True
        task.replace_existing = True
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)
    imported = 0
    for task in tasks:
        texture = unreal.load_asset("/Game/Imported/Fx/Textures/" + task.destination_name)
        if isinstance(texture, unreal.Texture2D):
            # Marks on a surface: never streamed out to a blur, never wrapped at the edge.
            texture.set_editor_property("never_stream", True)
            texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
            editor_assets.save_loaded_asset(texture, only_if_is_dirty=False)
            imported += 1
        else:
            log("projection texture NOT imported: " + task.filename)
    log("projection textures imported: %d (%s)" % (imported, ", ".join(sorted(pictures))))


def import_meshes():
    folder = os.path.join(CONTENT_ROOT, "shared", "fx", "meshes")
    if not os.path.isdir(folder):
        log("no effect meshes at " + folder)
        return
    tasks = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".glb"):
            continue
        task = unreal.AssetImportTask()
        task.filename = os.path.join(folder, name)
        task.destination_path = "/Game/Imported/Fx"
        task.automated = True
        task.save = True
        task.replace_existing = True
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)
    meshes = [p for p in editor_assets.list_assets("/Game/Imported/Fx", recursive=True)
              if isinstance(unreal.load_asset(p), unreal.StaticMesh)]
    # The importer makes every mesh a Nanite mesh. Nanite draws only opaque and masked materials, so a mesh that
    # wears glass or a glow (the first-aid box's window, laser beams) is drawn the ordinary way: the same rule
    # as the map import (Tools/Import/import_map.py) and the game's level state.
    nanite_blends = (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED)
    ordinary = 0
    for path in meshes:
        mesh = unreal.load_asset(path)
        see_through = any(slot.material_interface and slot.material_interface.get_blend_mode() not in nanite_blends
                          for slot in mesh.static_materials)
        settings = mesh.get_editor_property("nanite_settings")
        if settings.enabled == see_through:
            settings.enabled = not see_through
            mesh.set_editor_property("nanite_settings", settings)
            editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        ordinary += 1 if see_through else 0
    log("effect meshes drawn the ordinary way, not by Nanite (glass, glow): %d" % ordinary)
    log("effect meshes imported: %d (%s ...)" % (len(meshes), ", ".join(m.rsplit("/", 1)[1].split(".")[0] for m in meshes[:5])))


def build_goggle_glow_hue():
    """The goggle lenses (M_ScctGoggleGlow of Tools/Import/build_equipment_assets.py) with their colour turned
    round the grey axis by HueTurn radians: a player's light cosmetic (Play/ScctPawnLookSubsystem.cpp). The same
    rule as the Godot client's GoggleGlowView, so the lens keeps its brightness and only its hue moves."""
    material = new_material("M_ScctGoggleGlowHue")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    try_set(material, "used_with_skeletal_mesh", True)

    lens = lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -200)
    lens.set_editor_property("parameter_name", "Lens")
    intensity = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1100, 100)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 1.6)
    fade = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1100, 200)
    fade.set_editor_property("parameter_name", "Fade")
    fade.set_editor_property("default_value", 1.0)
    turn = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1100, 300)
    turn.set_editor_property("parameter_name", "HueTurn")
    turn.set_editor_property("default_value", 0.0)

    masked = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -850, -150)
    connect(lens, "RGB", masked, "A")
    connect(lens, "A", masked, "B")
    rotate = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -650, -100)
    rotate.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in ("C", "H"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    rotate.set_editor_property("inputs", pins)
    # Rodrigues' rotation of the colour about (1,1,1): the hue turns, the grey level stays.
    rotate.set_editor_property("code", "float3 k = float3(0.57735, 0.57735, 0.57735); float c = cos(H); float s = sin(H);"
                               " return max(C * c + cross(k, C) * s + k * dot(k, C) * (1.0 - c), 0.0);")
    connect(masked, "", rotate, "C")
    connect(turn, "", rotate, "H")
    lit = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, -50)
    connect(rotate, "", lit, "A")
    connect(intensity, "", lit, "B")
    out = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -200, 0)
    connect(lit, "", out, "A")
    connect(fade, "", out, "B")
    connect_output(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(material)


build_glow()
build_smoke()
build_cloud()
build_decal()
build_solid()
build_projection()
build_smoke_volume()
build_goggle_glow_hue()
if CONTENT_ROOT:
    import_projection_textures()
    import_meshes()
log("effect assets done")
