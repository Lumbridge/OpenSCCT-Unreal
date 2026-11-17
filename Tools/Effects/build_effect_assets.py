"""Builds the materials of the effects (Source/OpenSCCT/Play/ScctEffects*) and imports the game's effect meshes.
Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="<full path>/build_effect_assets.py [<content root>] [--force]"
(forward slashes in every path: the script switch reads a backslash as an escape)

The materials go to /Game/OpenSCCT/Effects/ and are committed: they are ours, made from nothing but maths.
  M_ScctFxGlow    additive unlit sprite; colour and opacity per instance (the first four custom data floats) and
                  the distance in cm over which it dims into a surface behind it (the fifth)
  M_ScctFxSmoke   soft translucent sprite lit by the scene; colour and opacity per instance
  M_ScctFxDecal   deferred decal: a dark hole in a paler chipped ring; parameters Color, RimColor and Strength
  M_ScctFxSolid   opaque surface with a glow, for stand-in shapes; parameters Color and Glow

With a content root the original effect meshes (grenades, mines, gadgets, beams: shared/fx/meshes/*.glb) are
imported to /Game/Imported/Fx/<Package>/ (never committed: original game content). An existing material is
rebuilt only with --force as the last argument.
"""
import os
import sys

import unreal

ARGS = [a for a in sys.argv[1:] if a != "--force"]
FORCE = "--force" in sys.argv
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
        if not FORCE:
            return None
        editor_assets.delete_asset(path)
    return asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def finish(material):
    lib.recompile_material(material)
    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
    log("material built: " + material.get_path_name())


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
    connect_output(soft, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
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


def build_decal():
    material = new_material("M_ScctFxDecal")
    if not material:
        return
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    # A dark hole in the middle of a paler ring of chipped, scuffed surface, so the mark reads on dark wood and
    # on white plaster alike.
    disc = soft_disc(material, -1700, 300, 1.0)
    hole = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -250)
    hole.set_editor_property("parameter_name", "Color")
    hole.set_editor_property("default_value", unreal.LinearColor(0.008, 0.007, 0.006, 1.0))
    rim = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -50)
    rim.set_editor_property("parameter_name", "RimColor")
    rim.set_editor_property("default_value", unreal.LinearColor(0.42, 0.38, 0.33, 1.0))
    inner = lib.create_material_expression(material, unreal.MaterialExpressionAdd, -900, 150)
    inner.set_editor_property("const_b", -0.5)
    connect(disc, "", inner, "A")
    sharp = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -750, 150)
    sharp.set_editor_property("const_b", 8.0)
    connect(inner, "", sharp, "A")
    middle = lib.create_material_expression(material, unreal.MaterialExpressionSaturate, -600, 150)
    connect(sharp, "", middle, "")
    colour = lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -450, -100)
    connect(rim, "", colour, "A")
    connect(hole, "", colour, "B")
    connect(middle, "", colour, "Alpha")
    connect_output(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

    wide = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -550, 300)
    wide.set_editor_property("const_b", 2.4)
    connect(disc, "", wide, "A")
    cut = lib.create_material_expression(material, unreal.MaterialExpressionSaturate, -400, 300)
    connect(wide, "", cut, "")
    strength = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -550, 480)
    strength.set_editor_property("parameter_name", "Strength")
    strength.set_editor_property("default_value", 0.92)
    opacity = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 350)
    connect(cut, "", opacity, "A")
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
    log("effect meshes imported: %d (%s ...)" % (len(meshes), ", ".join(m.rsplit("/", 1)[1].split(".")[0] for m in meshes[:5])))


build_glow()
build_smoke()
build_decal()
build_solid()
if CONTENT_ROOT:
    import_meshes()
log("effect assets done")
