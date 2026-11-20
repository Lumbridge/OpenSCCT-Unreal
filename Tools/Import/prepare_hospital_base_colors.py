"""Prepare user-supplied higher-resolution base colours without changing the original pack."""
import hashlib
import json
from pathlib import Path
import re
import sys

from PIL import Image


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def texture_key(name):
    return re.sub(r"[^a-z0-9]", "", Path(name).stem.lower())


def prepare(manifest, inventory, stock, out):
    inputs = read(manifest)
    audit = read(inventory)
    if audit["map"] != "MounE" or not isinstance(inputs, dict) or not inputs:
        raise ValueError("Expected the MounE audit and a nonempty {original.png: upscaled.png} manifest")
    out.mkdir(parents=True, exist_ok=True)
    planned = {}
    for name, replacement in sorted(inputs.items()):
        entry = audit["textures"].get(name)
        if not entry or Path(name).name != name or Path(name).suffix.lower() != ".png":
            raise ValueError("Unknown original Mount Hospital PNG: " + name)
        if any(use.get("role") == "Baked lightmap page" for use in entry["uses"]):
            print(f"{name}: reserved for the separate lightmap import")
            continue
        uses = [use for use in entry["uses"] if use.get("material")]
        original = stock / name
        if hashlib.sha256(original.read_bytes()).hexdigest() != entry["sha256"]:
            raise ValueError("Installed texture differs from the audit: " + name)
        replacement = manifest.parent / Path(replacement)
        file = "T_MounE_" + re.sub(r"[^A-Za-z0-9_]", "_", original.stem) + "_Base.png"
        key = texture_key(name)
        if key in planned:
            raise ValueError("Ambiguous staged base colour: " + name)
        with Image.open(original) as source, Image.open(replacement) as upscaled:
            if upscaled.format != "PNG":
                raise ValueError("Replacement must be a PNG: " + str(replacement))
            scale_x, rem_x = divmod(upscaled.width, source.width)
            scale_y, rem_y = divmod(upscaled.height, source.height)
            if scale_x != scale_y or scale_x < 2 or scale_x & (scale_x - 1) or rem_x or rem_y:
                raise ValueError(f"{name}: expected a 2x or higher power-of-two upscale with unchanged UV aspect")
            # Ignore generated alpha. Only the authored alpha is carried into the replacement.
            alpha = source.convert("RGBA").getchannel("A")
            image = upscaled.convert("RGB")
            if alpha.getextrema() != (255, 255):
                image.putalpha(alpha.resize(image.size, Image.Resampling.NEAREST))
            image.save(out / file)
        planned[key] = {"source": name, "file": file, "uses": uses}
    if not planned:
        raise ValueError("No original images eligible for the map colour import")
    plan = out / "base-color-plan.json"
    temporary = plan.with_suffix(".tmp")
    temporary.write_text(json.dumps({"map": "MounE", "textures": planned}, indent=2) + "\n", encoding="utf-8")
    temporary.replace(plan)
    print(f"Prepared {len(planned)} Mount Hospital base colour images.")


if __name__ == "__main__":
    prepare(*(Path(arg) for arg in sys.argv[1:]))
