"""Rebuilds the surface master material alone, with the lighting plan's parameter collection.

Runs inside the editor's Python (Tools/Materials/Build-SurfaceMaterial.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_surface_material.py"

Writes /Game/OpenSCCT/Materials/M_ScctSurface (and /Game/OpenSCCT/Materials/MPC_ScctLight when it is missing) from
Tools/Import/scct_materials.py. The glass, water and sky masters are left as they are. No map needs importing again:
the instances of the imported maps and of the run-time level hold their parent by its path and their values by name,
and the rebuilt master keeps every parameter it had.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Import"))
import scct_materials  # noqa: E402  pylint: disable=wrong-import-position

before = None
if unreal.EditorAssetLibrary.does_asset_exist(scct_materials.SURFACE):
    old = unreal.load_asset(scct_materials.SURFACE)
    library = unreal.MaterialEditingLibrary
    before = set(str(name) for getter in (library.get_scalar_parameter_names, library.get_vector_parameter_names,
                                           library.get_texture_parameter_names, library.get_static_switch_parameter_names)
                 for name in getter(old))
    del old

material = scct_materials.build_surface()
library = unreal.MaterialEditingLibrary
after = set(str(name) for getter in (library.get_scalar_parameter_names, library.get_vector_parameter_names,
                                      library.get_texture_parameter_names, library.get_static_switch_parameter_names)
            for name in getter(material))
if before is not None:
    lost = sorted(before - after)
    unreal.log_warning("SCCT-IMPORT surface master: %d parameters before, %d now, lost: %s, new: %s" % (
        len(before), len(after), ", ".join(lost) or "none", ", ".join(sorted(after - before)) or "none"))
    if lost:
        raise RuntimeError("the rebuilt surface material lost parameters its instances set: " + ", ".join(lost))
unreal.log_warning("SCCT-IMPORT surface master ready")
