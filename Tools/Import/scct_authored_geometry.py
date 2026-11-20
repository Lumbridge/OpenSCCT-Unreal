"""Expose the editor's linear COLOR_1 as two UV pairs before Interchange indexes/welds the mesh."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile
from urllib.parse import unquote

DIR_FLAG_SLOT = 28  # ScctAuthored::DirectionFlagSlot


def restore_painted_mesh_roles(actors, root, source_names):
    """Persist the authored vertex-colour role on existing imported and Lightmass-duplicated components."""
    import unreal
    from scct_materials import BAKED_VERTEX_SLOT
    names = {re.sub(r"[^A-Za-z0-9_]", "_", name).casefold() for name in source_names}
    count = 0
    if not names:
        return count
    for actor in actors:
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.static_mesh
            if not mesh:
                continue
            folder = mesh.get_path_name().rsplit("/", 1)[0]
            name = mesh.get_name().casefold()
            if folder == root + "/NativeLighting":
                name = re.sub(r"_(?:[0-9a-f]{8}|[0-9a-f]{12})$", "", name)
            elif folder != root + "/world/StaticMeshes":
                continue
            if name in names:
                values = component.get_editor_property("custom_primitive_data").get_editor_property("data")
                if len(values) <= BAKED_VERTEX_SLOT or values[BAKED_VERTEX_SLOT] != 2.0:
                    component.modify()
                    component.set_default_custom_primitive_data_float(BAKED_VERTEX_SLOT, 2.0)
                    count += 1
    return count


def direction_geometry(path, document, cache_root, log):
    """Return an importable world.glb and the names of meshes carrying the direction layout."""
    meshes = []
    for mesh in document.get("meshes", []):
        attributes = [p["attributes"] for p in mesh["primitives"]]
        if not any("COLOR_1" in a for a in attributes):
            continue
        if any("TEXCOORD_2" in a or "TEXCOORD_3" in a for a in attributes):
            log("Authored direction skipped on %s: UV2/3 already in use" % mesh["name"])
            continue
        meshes.append(mesh["name"])
    if not meshes:
        return path, set()

    source = Path(path).resolve()
    dest = Path(cache_root) / hashlib.sha256(os.fsencode(source)).hexdigest()[:20]
    dest.mkdir(parents=True, exist_ok=True)
    # Keep the same stem (the import destination is named after it) and the source map untouched.
    # Retain the derived source so Unreal's Reimport can reopen the same mesh and its relative images.
    doc = copy.deepcopy(document)
    with source.open("rb") as f:
        f.seek(12)
        json_size, = struct.unpack("<I", f.read(4))
        f.seek(20 + json_size)
        bin_size, tag = struct.unpack("<I4s", f.read(8))
        binary = bytearray(f.read(bin_size))
    if tag != b"BIN\0" or len(binary) != bin_size:
        raise ValueError("Missing binary chunk in " + str(source))

    accessors = doc["accessors"]
    views = doc["bufferViews"]
    zeros = {}
    def zero_uv(count):
        if count not in zeros:
            start = len(binary)
            binary.extend(bytes(8 * count))
            views.append(dict(buffer=0, byteOffset=start, byteLength=8*count, target=34962))
            zeros[count] = len(accessors)
            accessors.append(dict(bufferView=len(views)-1, componentType=5126, count=count, type="VEC2"))
        return zeros[count]

    pairs = {}
    for mesh in doc["meshes"]:
        if mesh["name"] not in meshes:
            continue
        for prim in mesh["primitives"]:
            a = prim["attributes"]
            index = a.get("COLOR_1")
            if index is None:
                continue  # GLTFCore fills missing UVs in sibling primitives with zero.
            acc = accessors[index]
            count = accessors[a["POSITION"]]["count"]
            if acc["type"] != "VEC4" or acc["count"] != count or "sparse" in acc:
                raise ValueError("Invalid editor direction accessor in " + mesh["name"])
            if index not in pairs:
                view = views[acc["bufferView"]]
                width = {5126: 4, 5123: 2, 5121: 1}[acc["componentType"]]
                if width != 4 and not acc.get("normalized"):
                    raise ValueError("Integer COLOR_1 must be normalized in " + mesh["name"])
                # Alias both halves of the same stream. Vertex attributes must start on 4-byte boundaries.
                stride = view.get("byteStride", width * 4)
                start = acc.get("byteOffset", 0)
                if width == 1:
                    # Expand normalized bytes to aligned float pairs; editor v1 normally writes floats.
                    if view.get("buffer", 0) != 0:
                        raise ValueError("Expected editor direction data in the GLB binary chunk")
                    pos = len(binary)
                    for vertex in range(count):
                        at = view.get("byteOffset", 0) + start + vertex * stride
                        binary.extend(struct.pack("<4f", *(v / 255.0 for v in binary[at:at+4])))
                    views.append(dict(buffer=0, byteOffset=pos, byteLength=16*count, byteStride=16, target=34962))
                    start, width, pair_type = 0, 4, 5126
                else:
                    views.append(dict(view, byteStride=stride))
                    pair_type = acc["componentType"]
                pair = []
                for offset in (0, width * 2):
                    entry = dict(componentType=pair_type, count=count, type="VEC2",
                                 bufferView=len(views)-1, byteOffset=start+offset)
                    if pair_type != 5126:
                        entry["normalized"] = True
                    pair.append(len(accessors))
                    accessors.append(entry)
                pairs[index] = pair
            # GLTFCore consumes contiguous UV channels, including for a mesh without a lightmap UV.
            for n in ("TEXCOORD_0", "TEXCOORD_1"):
                if n not in a:
                    a[n] = zero_uv(count)
            a["TEXCOORD_2"], a["TEXCOORD_3"] = pairs[index]

    doc["buffers"][0]["byteLength"] = len(binary)
    for uri in {item.get("uri") for item in doc.get("images", []) + doc.get("buffers", [])}:
        if not uri or uri.startswith("data:"):
            continue
        rel = Path(unquote(uri))
        if rel.anchor or ".." in rel.parts or ":" in str(rel):
            raise ValueError("Expected a relative editor asset: " + uri)
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        original = source.parent / rel
        if target.exists():
            src_stat, dst_stat = original.stat(), target.stat()
            if (src_stat.st_size, src_stat.st_mtime_ns) == (dst_stat.st_size, dst_stat.st_mtime_ns):
                continue
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as staging:
            temp = Path(staging.name)
        try:
            shutil.copy2(original, temp)
            os.replace(temp, target)
        finally:
            temp.unlink(missing_ok=True)

    encoded = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    with tempfile.NamedTemporaryFile(dir=dest, delete=False) as f:
        f.write(struct.pack("<4sIIII", b"glTF", 2, 28+len(encoded)+len(binary), len(encoded), 0x4e4f534a))
        f.write(encoded)
        f.write(struct.pack("<I4s", len(binary), b"BIN\0"))
        f.write(binary)
    os.replace(f.name, dest / source.name)
    return str(dest / source.name), set(meshes)
