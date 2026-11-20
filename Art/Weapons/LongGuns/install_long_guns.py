"""Install the four long-gun exports and their referenced textures into an existing rifle pack.

The content builder uses this same folder layout. Its stock and optional rifles may have
been updated independently, so merge only the four entries owned by this art set.
"""
import argparse
import json
from pathlib import Path
import os
import shutil
import struct
from tempfile import NamedTemporaryFile


def install(source, target, weapon=None):
    new = json.loads((source / "models.json").read_text(encoding="utf-8"))
    if weapon is not None:
        new = {weapon: new[weapon]}
    manifest = target / "models.json"
    current = json.loads(manifest.read_text(encoding="utf-8-sig")) if manifest.exists() else {}
    files = set()
    for key, stats in new.items():
        icon = source / "textures" / (key + "_icon.png")
        if icon.is_file():
            files.add(icon)
        for variant in ("", "_first"):
            mesh = source / (key + variant + ".glb")
            with mesh.open("rb") as stream:
                header = stream.read(20)
                if header[:4] != b"glTF":
                    raise ValueError(f"Invalid glTF file {mesh}")
                doc = json.loads(stream.read(struct.unpack_from("<I", header, 12)[0]))
            files.add(mesh)
            files.update(source / image["uri"] for image in doc["images"])
        current[key] = {field:stats[field] for field in ("triangles", "muzzle")}
    missing = [str(p) for p in files if not p.is_file()]
    if missing:
        raise ValueError("Missing source files: " + ", ".join(missing))
    for src in sorted(files):
        dest = target / src.relative_to(source)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
    with NamedTemporaryFile("w", dir=target, delete=False, encoding="utf-8") as tmp:
        tmp.write(json.dumps(current, indent=2) + "\n")
    os.replace(tmp.name, manifest)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", type=Path)
    parser.add_argument("--source", type=Path, default=Path(__file__).parent / "Export")
    parser.add_argument("--weapon", choices=('compactsmg', 'pumpshotgun', 'boltrifle', 'designatedmarksman'))
    options = parser.parse_args()
    install(options.source, options.target, options.weapon)
