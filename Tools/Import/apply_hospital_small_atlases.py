from pathlib import Path
import re
import sys
import unreal

from apply_hospital_base_colors import import_textures, log, read

plan, snapshot = map(Path, sys.argv[1:])
selections = read(plan)["textures"]
existing = read(snapshot)["bindings"]
originals = {}
for bindings in existing.values():
    for original, _ in bindings.values():
        name = original.rsplit(".", 1)[-1]
        key = re.sub(r"[^a-z0-9]", "", name.lower())
        if key in selections:
            originals[key] = unreal.load_asset(original)
if originals.keys() != selections.keys() or not all(isinstance(a, unreal.Texture2D) for a in originals.values()):
    raise ValueError("The planned small atlases must be active Hospital colour textures")
textures = import_textures(plan, selections, originals)
log("Verified new artwork for " + ", ".join(asset.get_name() for asset in textures.values()))
