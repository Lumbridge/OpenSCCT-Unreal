"""Builds the project's own equipment assets under /Game/OpenSCCT/Weapons (committed: nothing here is original
game content). Runs inside the editor's Python, with no window:

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_equipment_assets.py" -nullrhi

  Materials/M_ScctGlow         a soft self-lit glow, bright at the middle of a shape and gone at its edge: laser
                               beams and dots, the muzzle flash, torch coronas
  Materials/M_ScctGoggleGlow   the lit goggle lenses (parent of the lens materials the character import makes)
  Materials/M_ScctCloak        the camo suit: the body turns to bent light with a faint rim
  DA_ScctWeapons               which mesh stands for each simulation weapon class and where its sockets are
                               (UScctWeaponSet, Source/OpenSCCT/Play/ScctEquipmentData.h). Made once and then left
                               alone, so edits made in the editor are kept; delete it to get the defaults back.

Pass "rebuild" to make the materials again from this script.
"""
import sys

import unreal

FOLDER = "/Game/OpenSCCT/Weapons"
MATERIALS = FOLDER + "/Materials"
REBUILD = "rebuild" in sys.argv[1:]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
materials_lib = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def new_material(name):
    """An empty material to build, or None when it exists and is to be kept. A rebuild empties the existing
    asset instead of replacing it, so the instances made of it keep their parent."""
    path = MATERIALS + "/" + name
    if editor_assets.does_asset_exist(path):
        if not REBUILD:
            return None
        material = unreal.load_asset(path)
        materials_lib.delete_all_material_expressions(material)
        return material
    return asset_tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())


def finish(material):
    materials_lib.recompile_material(material)
    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
    log("made " + material.get_path_name())


def scalar(material, name, value, x, y):
    node = materials_lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def vector(material, name, value, x, y):
    node = materials_lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*value))
    return node


def binary(material, cls, a, b, x, y, a_pin="", b_pin=""):
    node = materials_lib.create_material_expression(material, cls, x, y)
    materials_lib.connect_material_expressions(a, a_pin, node, "A")
    materials_lib.connect_material_expressions(b, b_pin, node, "B")
    return node


def facing(material, x, y):
    """1 where the surface faces the camera, 0 at its silhouette."""
    normal = materials_lib.create_material_expression(material, unreal.MaterialExpressionPixelNormalWS, x, y)
    camera = materials_lib.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, x, y + 120)
    dot = binary(material, unreal.MaterialExpressionDotProduct, normal, camera, x + 250, y + 40)
    saturate = materials_lib.create_material_expression(material, unreal.MaterialExpressionSaturate, x + 450, y + 40)
    materials_lib.connect_material_expressions(dot, "", saturate, "")
    return saturate


def in_the_picture(material, source, x, y):
    """`source` as a brightness in the finished picture instead of in the scene: divided by the exposure the eye
    has settled on, within bounds (the same rule as Graph.in_the_picture in Tools/Import/scct_materials.py). A
    flash or a beam is then as bright on screen in a dark cellar as in a lit hall, and never a white-out."""
    inverse = materials_lib.create_material_expression(material, unreal.MaterialExpressionEyeAdaptationInverse, x, y)
    one = materials_lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x - 200, y + 120)
    one.set_editor_property("constant", unreal.LinearColor(1, 1, 1, 1))
    if not (materials_lib.connect_material_expressions(one, "", inverse, "LightValueInput")
            or materials_lib.connect_material_expressions(one, "", inverse, "LightValue")):
        log("NOT CONNECTED: the eye adaptation node of " + material.get_name())
    custom = materials_lib.create_material_expression(material, unreal.MaterialExpressionCustom, x + 220, y)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in ("L", "K"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    custom.set_editor_property("code", "float s = (K.r + K.g + K.b) / 3.0; return L * clamp(s, 0.08, 16.0);")
    materials_lib.connect_material_expressions(source, "", custom, "L")
    materials_lib.connect_material_expressions(inverse, "", custom, "K")
    return custom


def build_glow():
    material = new_material("M_ScctGlow")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", False)

    colour = vector(material, "Color", (1.0, 0.05, 0.03, 1.0), -900, -200)
    intensity = scalar(material, "Intensity", 20.0, -900, 0)
    sharpness = scalar(material, "Sharpness", 3.0, -900, 100)
    fade = scalar(material, "Fade", 1.0, -900, 200)

    # A cylinder or a ball drawn this way reads as a beam or a point of light: full in the middle, nothing at
    # the edge, from every side.
    core = materials_lib.create_material_expression(material, unreal.MaterialExpressionPower, -250, 350)
    materials_lib.connect_material_expressions(facing(material, -900, 350), "", core, "Base")
    materials_lib.connect_material_expressions(sharpness, "", core, "Exp")

    lit = binary(material, unreal.MaterialExpressionMultiply, colour, intensity, -600, -100)
    faded = binary(material, unreal.MaterialExpressionMultiply, lit, fade, -400, -50)
    out = binary(material, unreal.MaterialExpressionMultiply, faded, core, -100, 0)
    materials_lib.connect_material_property(in_the_picture(material, out, 150, 0), "", MP.MP_EMISSIVE_COLOR)
    finish(material)


def build_goggle_glow():
    material = new_material("M_ScctGoggleGlow")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_skeletal_mesh", True)

    lens = materials_lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, -200)
    lens.set_editor_property("parameter_name", "Lens")
    # The lenses are drawn as short shafts of light whose texture fades along them: kept gentle, so the fade
    # survives and the bloom does not swallow the face.
    intensity = scalar(material, "Intensity", 1.6, -900, 100)
    fade = scalar(material, "Fade", 1.0, -900, 200)
    masked = binary(material, unreal.MaterialExpressionMultiply, lens, lens, -700, -150, a_pin="RGB", b_pin="A")
    lit = binary(material, unreal.MaterialExpressionMultiply, masked, intensity, -550, -100)
    shown = binary(material, unreal.MaterialExpressionMultiply, lit, fade, -300, -50)
    # Custom primitive data slot 2 (scct_materials.NEAR_FADE_SLOT): the body the lenses sit on thins out of the
    # picture under this distance from the camera (the local player's); its lenses, which are light and would
    # hang in the air where the head was, are out a little before the head starts to go.
    near = materials_lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 250)
    near.set_editor_property("parameter_name", "NearFade")
    near.set_editor_property("default_value", 0.0)
    near.set_editor_property("use_custom_primitive_data", True)
    near.set_editor_property("primitive_data_index", 2)
    depth = materials_lib.create_material_expression(material, unreal.MaterialExpressionPixelDepth, -700, 400)
    kept = materials_lib.create_material_expression(material, unreal.MaterialExpressionCustom, -400, 300)
    kept.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for name in ("Start", "Depth"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    kept.set_editor_property("inputs", pins)
    kept.set_editor_property("code", "return Start > 0.0 ? smoothstep(Start * 0.9, Start * 1.2, Depth) : 1.0;")
    materials_lib.connect_material_expressions(near, "", kept, "Start")
    materials_lib.connect_material_expressions(depth, "", kept, "Depth")
    out = binary(material, unreal.MaterialExpressionMultiply, shown, kept, -100, 0)
    materials_lib.connect_material_property(out, "", MP.MP_EMISSIVE_COLOR)
    finish(material)


def build_cloak():
    material = new_material("M_ScctCloak")
    if not material:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("used_with_skeletal_mesh", True)
    material.set_editor_property("two_sided", False)
    for name, value in (("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE),
                        ("refraction_method", unreal.RefractionMode.RM_INDEX_OF_REFRACTION)):
        try:
            material.set_editor_property(name, value)
        except Exception as error:  # pylint: disable=broad-except
            log("property not set: %s (%s)" % (name, error))

    base = materials_lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -300)
    base.set_editor_property("parameter_name", "BaseColor")
    amount = scalar(material, "Amount", 1.0, -1100, 0)

    rim = materials_lib.create_material_expression(material, unreal.MaterialExpressionFresnel, -1100, 200)
    rim.set_editor_property("exponent", 3.0)
    rim.set_editor_property("base_reflect_fraction", 0.02)

    def constant(value, x, y):
        node = materials_lib.create_material_expression(material, unreal.MaterialExpressionConstant, x, y)
        node.set_editor_property("r", value)
        return node

    def lerp(a, b, alpha, x, y):
        node = materials_lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
        materials_lib.connect_material_expressions(a, "", node, "A")
        materials_lib.connect_material_expressions(b, "", node, "B")
        materials_lib.connect_material_expressions(alpha, "", node, "Alpha")
        return node

    # The suit's own colour drains away as the camo comes on.
    dark = binary(material, unreal.MaterialExpressionMultiply, base, constant(0.1, -1100, -100), -800, -250, a_pin="RGB")
    colour = materials_lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -550, -300)
    materials_lib.connect_material_expressions(base, "RGB", colour, "A")
    materials_lib.connect_material_expressions(dark, "", colour, "B")
    materials_lib.connect_material_expressions(amount, "", colour, "Alpha")
    materials_lib.connect_material_property(colour, "", MP.MP_BASE_COLOR)

    # Fully on, only the rim of the shape holds a little of the light; the rest is what lies behind, bent.
    rim_opacity = binary(material, unreal.MaterialExpressionMultiply, rim, constant(0.1, -1100, 350), -800, 250)
    cloaked_opacity = binary(material, unreal.MaterialExpressionAdd, rim_opacity, constant(0.0, -800, 400), -600, 300)
    opacity = lerp(constant(1.0, -800, 100), cloaked_opacity, amount, -350, 150)
    materials_lib.connect_material_property(opacity, "", MP.MP_OPACITY)

    bend = lerp(constant(1.0, -800, 550), constant(1.2, -800, 650), amount, -350, 550)
    materials_lib.connect_material_property(bend, "", MP.MP_REFRACTION)

    materials_lib.connect_material_property(constant(0.25, -350, 750), "", MP.MP_ROUGHNESS)
    shimmer_colour = vector(material, "Shimmer", (0.35, 0.6, 1.0, 1.0), -1100, 800)
    shimmer = binary(material, unreal.MaterialExpressionMultiply, shimmer_colour, rim, -800, 850)
    shimmer_on = binary(material, unreal.MaterialExpressionMultiply, shimmer, amount, -600, 850)
    shimmer_out = binary(material, unreal.MaterialExpressionMultiply, shimmer_on, scalar(material, "ShimmerStrength", 0.02, -800, 1000), -350, 900)
    materials_lib.connect_material_property(shimmer_out, "", MP.MP_EMISSIVE_COLOR)
    finish(material)


def build_weapon_set():
    path = FOLDER + "/DA_ScctWeapons"
    if not hasattr(unreal, "ScctWeaponSet"):
        log("no ScctWeaponSet class: build the game's C++ first, then run this again for %s" % path)
        return
    if editor_assets.does_asset_exist(path):
        log("kept " + path)
        return

    originals = "/Game/Imported/Chars/SWeaponFirst/%s/SkeletalMeshes/%s.%s"
    new_rifle = "/Game/OpenSCCT/Weapons/MercRifle/SK_MercRifle.SK_MercRifle"

    def row(simulation_class, original, replacement="", first_person=False):
        entry = unreal.ScctWeaponPresentation()
        entry.set_editor_property("simulation_class", simulation_class)
        entry.set_editor_property("mesh", unreal.SoftObjectPath(originals % (original, original, original)))
        if replacement:
            entry.set_editor_property("replacement_mesh", unreal.SoftObjectPath(replacement))
        entry.set_editor_property("first_person", first_person)
        if replacement:
            entry.set_editor_property("attach_offset", rifle_offset(first_person))
            rifle_left_hand(entry, first_person)
        return entry

    def rifle_left_hand(entry, first_person):
        """Where the left hand holds the new rifle (the row's "Left hand" values; FScctEquipmentView bends the
        arm to put it there). The numbers are those -ScctWeaponProbe logs.

        On a body the original animations lay the left hand palm up under the original's pump: its wrist is at
        (33.5, -9.7, 0.4) from the gun bone, 12 cm ahead of the new rifle's short vertical fore grip, and a hand
        held palm up cannot close on a vertical grip. So the hand is put on the fore grip itself: reaching across
        from the left with the fingers pointing to the right, the palm on the grip's front and facing the shooter,
        the thumb up (the hand bones' X runs to the fingers, -Y out of the palm, -Z to the thumb). The wrist is
        8.5 cm to the left of the grip's middle, 3 cm in front of it and 3.5 cm under the socket.

        In the merc's own view the hand is on the fore grip too. The view model's hand is one rigid piece with open
        fingers, so it is laid against the grip's left side, the side the eye sees: fingers forward and 37 degrees
        to the right round the grip's front, the palm on the grip, the thumb up. The wrist is 5.5 cm to the left of
        the grip's middle, 3.5 cm behind it and 1.5 cm under it. With the rifle as rifle_offset holds it there the
        hand is inside the bottom of the picture. The animation's wrist is 2 cm in front of the grip and 3 cm to its
        left, so the arm is bent only a little."""
        arm = ("Bip01-L-UpperArm", "Bip01-L-Forearm", "Bip01-L-Hand") if first_person else ("B-L-UpperArm", "B-L-ForeArm", "B-L-Hand")
        for name, bone in zip(("left_upper_arm_bone", "left_forearm_bone", "left_hand_bone"), arm):
            entry.set_editor_property(name, bone)
        if first_person:
            entry.set_editor_property("original_left_hand", unreal.Vector(-32.5, 3.6, -6.2))
            entry.set_editor_property("left_hand_in_socket", unreal.Transform(
                unreal.Vector(-3.5, -5.5, -1.5), unreal.Rotator(roll=180.0, pitch=0.0, yaw=36.9), unreal.Vector(1.0, 1.0, 1.0)))
        else:
            entry.set_editor_property("original_left_hand", unreal.Vector(33.5, -9.7, 0.4))
            entry.set_editor_property("left_hand_in_socket", unreal.Transform(
                unreal.Vector(3.0, -8.5, -3.5), unreal.Rotator(roll=180.0, pitch=0.0, yaw=90.0), unreal.Vector(1.0, 1.0, 1.0)))

    def rifle_offset(first_person):
        """Where the new rifle sits on the original gun's bone. Worked out from the bones as a run logs them
        with -ScctWeaponProbe (Source/OpenSCCT/Play/ScctEquipmentView.h), not by eye.

        The new rifle is modelled with the middle of its pistol grip at the origin, the barrel along +X and up
        +Z; its bore is 9.5 cm above the origin.

        On a body (SWeaponFirst.M160) the gun bone "corps" is the right wrist with the original's barrel along
        +X and up +Z, the bore 10.2 cm above it and 2.2 cm to the -Y side (bone "FX"). The right hand's fingers
        close round a grip whose middle is 9 cm in front of the wrist. So the new rifle goes 9 cm forward, 2.2
        to the side and 0.7 up: grip in the right hand, bore on the original's bore. The left hand of the
        original animation holds the original's pump, 36 cm in front of the wrist: that is 27 cm in front of the
        new grip, under the handguard at the light module, 12 cm ahead of the new rifle's short fore grip. The
        hand cannot be brought back to the fore grip by an offset; that needs the arm posed.

        In the merc's own view (SWeaponFirst.1DEF_01) the same gun is turned in the bone: the barrel (bone "FX")
        runs along (-1.00, 0.10, 0.01), up is (0.10, 0.99, -0.08) and the gun's +Y is (0.01, 0.07, 1.00). The
        same place in that frame would be (-11.05, 3.45, -2.65): the wrist is at (-2.1, 2.0, -0.5), the grip 9 cm
        down the barrel from it. But in that view the right hand is below the picture (2 cm in front of the eye)
        and the left hand is the one seen, and with the rifle there its fore grip, where the left hand goes
        (rifle_left_hand), is about 40 degrees under the eye, at the bottom edge of the picture, and the hand on it
        went out of the picture. So there the rifle slides 6 cm further along its own barrel: bore on the original's
        bore line, the fore grip and the hand on it inside the picture, and the unseen right hand 6 cm behind the
        pistol grip."""
        if not first_person:
            return unreal.Transform(unreal.Vector(9.0, -2.2, 0.7), unreal.Rotator(0.0, 0.0, 0.0), unreal.Vector(1.0, 1.0, 1.0))
        forward = unreal.Vector(-0.995, 0.100, 0.010)
        right = unreal.Vector(0.010, 0.070, 0.997)
        up = unreal.Vector(0.100, 0.992, -0.080)
        rotation = unreal.MathLibrary.make_rotation_from_axes(forward, right, up)
        return unreal.Transform(unreal.Vector(-17.02, 4.05, -2.59), rotation, unreal.Vector(1.0, 1.0, 1.0))

    weapons = asset_tools.create_asset("DA_ScctWeapons", FOLDER, unreal.ScctWeaponSet, unreal.DataAssetFactory())
    weapons.set_editor_property("weapons", [
        # The merc's rifle as he sees it (arms and gun in one model) and as others see it.
        row("SWeaponDefense", "1DEF_01", new_rifle, True),
        row("SWeaponDefenseDummy", "M160", new_rifle),
        row("SWeaponDefense_Uzi", "1DEF_Uzi", "", True),
        row("SWeaponDefenseDummy_Uzi", "Hk81"),
        row("SWeaponDefense_Shotgun", "1DEF_shotgun", "", True),
        row("SWeaponDefenseDummy_Shotgun", "BM401"),
        row("SStickyShocker", "p190"),
    ])
    editor_assets.save_loaded_asset(weapons, only_if_is_dirty=False)
    log("made " + path)


build_glow()
build_goggle_glow()
build_cloak()
build_weapon_set()
