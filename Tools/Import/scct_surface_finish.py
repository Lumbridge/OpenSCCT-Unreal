"""Physical finishes for legacy map materials when the local merc wears the body camera."""
import unreal


# Perceptual roughness, metallic, legacy atlas normal scale, scanned microdetail scale.
# The latter two are multipliers on the material's existing normal and detail art.
CLASSES = {
    "concrete_bare": (.81, 0, .65, .90),
    "concrete_dirty": (.86, 0, .65, .90),
    "concrete_painted": (.55, 0, .48, .55),
    "plaster": (.82, 0, .50, .75),
    "brick": (.87, 0, .72, .82),
    "metal_bare": (.38, .96, .57, .65),
    "metal_painted": (.51, 0, .43, .65),
    "grating": (.43, .87, .65, .70),
    "rust": (.82, .04, .78, .92),
    "tiles": (.43, 0, .66, .60),
    "plastic": (.45, 0, .48, .58),
    "wood": (.72, 0, .68, .80),
    "wood_finished": (.46, 0, .55, .60),
    "paper": (.88, 0, .32, .40),
}

# The source catalogue groups whole atlases by a single class. A few have a different actual
# finish. Keep the legacy classes for the other presentation; suppress their mismatched details here.
SOURCES = {
    "Prison_TEX.BSP.FLO1_lvl1": ((.31, 0, .55, .28), "T_HospitalFloorFinish"),
    "Prison_TEX.BSP.wallfloor1": ((.58, 0, .32, 0), None),  # painted wall, previously varnished wood
    "Prison_TEX.BSP.WAL_newtech05": ((.60, 0, .55, 0), None),  # smooth wall panels
    "Prison_TEX.BSP.DOO_elevator01": ((.34, .94, .60, .35), None),  # brushed steel
    "Prison_TEX.Static.roomdoor": ((.51, 0, .38, 0), "T_HospitalDoorFinish"),
    "Prison_TEX.Static.murplaco": ((.88, 0, .55, 0), None),  # taped drywall
    "Prison_TEX.Static.Box1": ((.85, 0, .50, 0), None),  # cardboard and wood share this atlas
}


def bind(instance, source, class_name, look):
    if source in SOURCES:
        values, atlas = SOURCES[source]
    elif look.get("kind") == "surface":
        # Also covers future classes absent from this table with their authored legacy finish range.
        values = CLASSES.get(class_name, ((look["rough_dark"] + look["rough_bright"]) * .5,
                                          look["metallic"], .65, .75))
        atlas = None
    else:
        return False

    lib = unreal.MaterialEditingLibrary
    previous = lib.get_material_instance_vector_parameter_value(instance, "BodycamFinish")
    channels = (previous.r, previous.g, previous.b, previous.a)
    changed = any(abs(a - b) > 1e-5 for a, b in zip(channels, values))
    if changed:
        lib.set_material_instance_vector_parameter_value(instance, "BodycamFinish", unreal.LinearColor(*values))
    if atlas:
        tex = unreal.load_asset("/Game/OpenSCCT/Materials/Finishes/" + atlas)
        if not tex:
            raise RuntimeError(f"Missing authored finish atlas {atlas}")
        if lib.get_material_instance_texture_parameter_value(instance, "BodycamFinishAtlas") != tex:
            lib.set_material_instance_texture_parameter_value(instance, "BodycamFinishAtlas", tex)
            changed = True
    use_atlas = bool(atlas)
    if lib.get_material_instance_static_switch_parameter_value(instance, "UseBodycamFinishAtlas") != use_atlas:
        lib.set_material_instance_static_switch_parameter_value(instance, "UseBodycamFinishAtlas", use_atlas)
        changed = True
    return changed
