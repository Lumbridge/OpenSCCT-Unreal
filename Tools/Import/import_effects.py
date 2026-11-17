"""Builds the effect materials and the Niagara systems under /Game/OpenSCCT/Effects (committed). Runs inside the
editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="import_effects.py <textures folder>"

<textures folder> is what make_effect_textures.py wrote. Needed after a change to scct_effects.py, scct_niagara.py
or make_effect_textures.py; the materials and systems are deleted and made again (one that exists cannot be
re-wired in place). Re-import the maps afterwards (Import-Map.ps1).
"""
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scct_effects  # noqa: E402  pylint: disable=wrong-import-position
import scct_niagara  # noqa: E402  pylint: disable=wrong-import-position

TEXTURES = sys.argv[1]

tasks = []
for file in sorted(os.listdir(TEXTURES)):
    if file.endswith(".png"):
        task = unreal.AssetImportTask()
        task.filename = os.path.join(TEXTURES, file)
        task.destination_path = scct_effects.EFFECTS
        task.automated = True
        task.save = True
        task.replace_existing = True
        tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

scct_effects.build_all()
scct_niagara.build_all()
unreal.log_warning("SCCT-IMPORT effects ready")
