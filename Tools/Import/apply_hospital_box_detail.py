"""Remove the stock wood overlay from Mount Hospital's mixed cardboard and wood box atlas."""

import math

import unreal


MATERIAL_PATH = "/Game/Imported/Maps/MounE/Materials/MI_Prison_TEX_Box1"
MASTER_PATH = "/Game/OpenSCCT/Materials/M_ScctSurface.M_ScctSurface"
STRENGTHS = {
    # Keep a little relief from the normal derived from the atlas, at the paper class's strength.
    "MacroNormal": 0.2,
    # These branches apply one wood overlay over every UV region, including the printed cardboard.
    "DetailNormalStrength": 0.0,
    "DetailColour": 0.0,
    "DetailRoughness": 0.0,
    "DetailOcclusion": 0.0,
}


def main():
    instance = unreal.load_asset(MATERIAL_PATH)
    if not isinstance(instance, unreal.MaterialInstanceConstant):
        raise ValueError(f"The copied Mount Hospital box material is missing: {MATERIAL_PATH}")
    parent = instance.get_editor_property("parent")
    if not parent or parent.get_path_name() != MASTER_PATH:
        raise ValueError("The box material needs the imported map's surface master")

    editing = unreal.MaterialEditingLibrary
    available = {str(name) for name in editing.get_scalar_parameter_names(instance)}
    missing = STRENGTHS.keys() - available
    if missing:
        raise ValueError(f"The box material is missing scalar parameters: {', '.join(sorted(missing))}")

    changed = []
    for parameter, value in STRENGTHS.items():
        previous = editing.get_material_instance_scalar_parameter_value(instance, parameter)
        if math.isclose(previous, value, abs_tol=1e-6):
            continue
        # UE currently returns false even when this editor setter updates the value.
        editing.set_material_instance_scalar_parameter_value(instance, parameter, value)
        actual = editing.get_material_instance_scalar_parameter_value(instance, parameter)
        if not math.isclose(actual, value, abs_tol=1e-6):
            raise RuntimeError(f"The box material's {parameter} did not update")
        changed.append(f"{parameter} {previous:.3g} -> {actual:.3g}")

    if changed and not unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False):
        raise RuntimeError("Could not save the copied Mount Hospital box material")
    summary = ", ".join(changed) if changed else "already applied"
    unreal.log_warning(f"SCCT-IMPORT {instance.get_name()}: {summary}")


if __name__ == "__main__":
    main()
