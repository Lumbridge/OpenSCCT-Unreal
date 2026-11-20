"""Fit four licensed game-art meshes to the merc character's coordinate system.

Build-LongGuns.ps1 reads the Blender, FBX and OBJ artist files into the intermediate
directory. The SPAS-12 is read directly from its original textured glTF. A single
model can be rebuilt with --weapon <key>.
The output is the weapon-only skinned glTF plus external authored PBR maps used by
both views. Coordinates below are mesh landmarks sampled from the art.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("work", type=Path)
parser.add_argument("svm", type=Path)
parser.add_argument("--output", type=Path, default=Path(__file__).parent / "Export")
parser.add_argument("--weapon", choices=('compactsmg', 'pumpshotgun', 'boltrifle', 'designatedmarksman'))
args = parser.parse_args()
sys.path.insert(0, str(args.svm / "content"))
from svmbuild.sidearm_models import append, write
from svmbuild.weapon_icons import render as render_icon
from spas12 import MATERIALS as SPAS12_MATERIALS, read_spas12

output = args.output
maps = output / "textures"
maps.mkdir(parents=True, exist_ok=True)
source = Path(__file__).parent / "Sources"
library = {}


def resize(path, maximum):
    image = Image.open(path).convert("RGB")
    image.thumbnail((maximum, maximum), Image.Resampling.LANCZOS)
    return image


def painted_surface(key, folder, color, normal, gloss, spec):
    stem = key + "_"
    resize(folder / color, 2048).save(maps / (stem + "Color.jpg"), quality=94, subsampling=0)
    resize(folder / normal, 2048).save(maps / (stem + "Normal.png"))
    specular = np.asarray(resize(folder / spec, 1024).convert("L"), dtype=float) / 255
    smooth = np.asarray(resize(folder / gloss, 1024).convert("L"), dtype=float) / 255
    albedo = np.asarray(resize(folder / color, 1024), dtype=float)
    warm_wood = np.clip((albedo[:, :, 0] - albedo[:, :, 2] - 6) / 25, 0, 1)
    finish = np.empty((*specular.shape, 3), dtype=np.uint8)
    finish[:, :, 0] = 255
    finish[:, :, 1] = np.rint(np.clip(.82 - .56 * smooth, .24, .84) * 255)
    finish[:, :, 2] = np.rint(np.clip(.12 + specular * .85, 0, .88) * (1 - warm_wood) * 255)
    Image.fromarray(finish).save(maps / (stem + "ORM.png"))
    library[key] = [stem + "Color.jpg", stem + "Normal.png", stem + "ORM.png"]


def tiled_surfaces():
    """Original timber, coated steel and molded polymer tiles for models supplied without PBR maps."""
    size = 768
    rng = np.random.default_rng(44820)
    yy, xx = np.indices((size, size)) * (2 * np.pi / size)
    grit = rng.normal(0, 1.5, (size, size))
    wood = np.sin(15 * yy + 1.6 * np.sin(xx) + .7 * np.sin(2 * xx + yy))
    grain = np.sin(90 * yy + 3 * wood + 2 * np.cos(4 * xx)) * .55 + wood
    profiles = [
        ("Walnut", [87, 55, 37], grain * 6 + grit, .64, .0),
        ("Parkerized", [58, 63, 65], grit + np.sin(yy * 160) * .9, .50, .66),
        ("Polymer", [47, 50, 48], grit * 1.1 + np.cos(yy * 48 + xx * 48) * .4, .74, .015),
        ("Receiver", [71, 74, 72], grit + 2 * np.sin(yy * 115), .44, .71),
        ("Grip", [47, 46, 44], grit + 2.5 * np.sin(xx * 72) * np.sin(yy * 72), .83, .015),
        ("ShellRed", [124, 42, 29], grit + np.sin(yy * 110) * .8, .67, .0),
        ("ShellBrass", [146, 107, 54], grit + np.sin(yy * 120), .36, .76),
    ]
    for name, color, variation, roughness, metal in profiles:
        if args.weapon == 'pumpshotgun' and not name.startswith('Shell'):
            continue
        prefix = "Finish" + name
        rgb = np.clip(np.array(color) + variation[:, :, None], 0, 255).astype("uint8")
        Image.fromarray(rgb).save(maps / (prefix + "Color.jpg"), quality=94)
        ny, nx = np.gradient(variation)
        vector = np.stack((-nx*.055, -ny*.055, np.ones_like(nx)), axis=2)
        vector /= np.linalg.norm(vector, axis=2, keepdims=True)
        Image.fromarray(((vector*.5+.5)*255).astype("uint8")).save(maps / (prefix + "Normal.png"))
        packed = np.empty_like(rgb)
        packed[:, :, 0] = 255
        packed[:, :, 1] = np.clip(roughness*255 + variation*1.7, 5, 250)
        packed[:, :, 2] = round(metal*255)
        Image.fromarray(packed).save(maps / (prefix + "ORM.png"))
        library[name] = [prefix + "Color.jpg", prefix + "Normal.png", prefix + "ORM.png"]


if args.weapon in (None, 'compactsmg'):
    painted_surface("Scorpion", source/"Scorpion", "Color.jpg", "Normal.png", "Gloss.png", "Spec.png")
if args.weapon in (None, 'boltrifle'):
    painted_surface("Enfield", source/"LeeEnfield", "Color.jpg", "Normal.png", "Spec.png", "Spec.png")
tiled_surfaces()

# The axis transform maps from source Blender space into +X forward, +Y up glTF space.
# Pivots are on visible art features; the authored game poses move only exterior parts.
GUNS = {
    "compactsmg": ("SMG", [.0,3.86,-1.15], [[0,-1,0],[0,0,1],[-1,0,0]], .029,
       {"FX":[0,-5.411,2.11],"Sight":[0,3.19,3.30],"Support":[0,-.32,-.86],"Bolt":[0,-.22,2.35],
        "Magazine":[0,.04,1.28],"MagazineGrip":[0,.06,-1.12],"BoltHandle":[.84,-1.43,2.53],
        "FX_DOUILLE":[-.8,-.46,2.24],"Rail":[-.93,-2.90,2.08]}),
    "pumpshotgun": ("Pump", [-.009,-.082,-.909], [[0,0,1],[0,1,0],[-1,0,0]], .37,
       {"FX":[-.009,.178,1.0],"Sight":[-.009,.235,-.890],"Support":[-.009,.055,-.014],"Pump":[-.009,.113,.22],
        "PumpGrip":[-.009,.055,-.014],"Bolt":[-.038,.202,-.405],"BoltHandle":[-.041,.202,-.405],
        "Shell":[-.009,.131,-.55],"ShellLoad":[-.009,.099,-.57],
        "FX_DOUILLE":[-.042,.201,-.407],"Rail":[-.043,.188,.32]}),
    "boltrifle": ("Bolt", [1.93,-.009,-.38], [[-1,0,0],[0,0,1],[0,1,0]], .118,
       {"FX":[-4.898,-.008,.260],"Sight":[1.52,-.008,.483],"Support":[-.72,-.008,-.24],
        "Optic":[1.65,-.008,.758],"Bolt":[1.20,-.008,.26],"BoltHandle":[1.66,.224,.226],
        "Magazine":[.80,0,-.33],"MagazineGrip":[.79,0,-.48],"FX_DOUILLE":[.63,.11,.24],"Rail":[-1.9,.17,.03]}),
    "designatedmarksman": ("DMR", [-4.84,-2.73,-.475], [[0,1,0],[0,0,1],[1,0,0]], .111,
       {"FX":[-4.84,4.99,.093],"Sight":[-4.84,-.50,.397],"Support":[-4.84,.43,-.14],
        "Optic":[-4.84,-2.68,.78],"Bolt":[-4.75,-1.1,.12],"BoltHandle":[-4.64,-.63,.058],
        "Magazine":[-4.84,-1.27,-.18],"MagazineGrip":[-4.84,-1.22,-.69],
        "FX_DOUILLE":[-4.67,-.86,.17],"Rail":[-4.68,1.09,.15]}),
}


def part_type(weapon, obj, island, material):
    group, surface = "corps", "Parkerized"
    if weapon == "compactsmg":
        return {"magazine":"Magazine","bolt":"Bolt"}.get(obj, "corps"), "Scorpion"
    if weapon == "boltrifle":
        if island in (15,16): # loose cartridge set next to the source model
            return None, None
        if island in (7,8,9,11,17,22):
            group = "Bolt"
        elif island == 3:
            group = "Magazine"
        return group, "Enfield"
    if weapon == "pumpshotgun":
        return obj, material
    if island in (18,19,20,21,26,27):
        return None, None  # the source scope and integral mounts are replaced with the selected game optic
    if island == 2:
        group = "Magazine"
    elif island in (24,25):
        group = "Bolt"
    if material == "Material":
        surface = "Walnut"
    elif material == "Material.004":
        surface = "Grip"
    elif island in (23,24,25):
        surface = "Receiver"
    return group, surface


def create_material(doc, material):
    indices = []
    for file in library[material]:
        index = len(doc["images"])
        doc["images"].append({"uri":"textures/" + file})
        doc["textures"].append({"source":index})
        indices.append(index)
    index = len(doc["materials"])
    doc["materials"].append({"name":material, "pbrMetallicRoughness":{
        "baseColorTexture":{"index":indices[0]}, "metallicRoughnessTexture":{"index":indices[2]}},
        "normalTexture":{"index":indices[1]}})
    if material in SPAS12_MATERIALS.values():
        doc['materials'][-1].update(occlusionTexture={'index': indices[2]}, doubleSided=True)
    return index


def reload_prop(center):
    """Small closed game prop. The animation can bring it out of its hidden rest location."""
    rings = [(-.025, .0), (-.025, .008), (-.018, .008), (.025, .008), (.025, .0)]
    steps = 24
    for k, ((x0, r0), (x1, r1)) in enumerate(zip(rings, rings[1:])):
        points, normals = [], []
        for i in range(steps):
            quad, direction = [], []
            for x, radius, angle in ((x0,r0,i), (x0,r0,i+1), (x1,r1,i+1), (x1,r1,i)):
                phi = angle * 2 * np.pi / steps
                quad.append(center + [x, radius*np.cos(phi), radius*np.sin(phi)])
                direction.append([0,np.cos(phi),np.sin(phi)] if x0 != x1 else [-1 if k == 0 else 1,0,0])
            for ix in ((0,1,2),(0,2,3)):
                if np.linalg.norm(np.cross(quad[ix[1]]-quad[ix[0]], quad[ix[2]]-quad[ix[0]])) < 1e-10:
                    continue
                points.append([quad[j] for j in ix])
                normals.append([direction[j] for j in ix])
        yield "ShellBrass" if k < 2 else "ShellRed", points, normals


def build(weapon):
    asset, origin, axes, scale, contacts = GUNS[weapon]
    raw = (read_spas12(source / 'Pump' / 'spas12.glb', maps, library) if weapon == 'pumpshotgun'
           else np.load(args.work / (asset + ".npz")))
    axes = np.array(axes, dtype=float)
    def position(point):
        return (np.asarray(point) - origin) @ axes.T * scale
    vertices, normals, coordinates = position(raw["xyz"]), raw["normals"] @ axes.T, raw["uv"].copy()
    anchors = {"corps":np.zeros(3),"Grip":np.zeros(3)}
    anchors.update({name:position(point) for name,point in contacts.items()})
    doc = {"asset":{"version":"2.0"}, "scene":0, "scenes":[{"nodes":[0]}], "nodes":[],
           "meshes":[], "skins":[], "materials":[], "images":[], "textures":[], "bufferViews":[], "accessors":[]}
    binary = bytearray()
    joint_ids = {name:i for i,name in enumerate(anchors)}
    parents = {"BoltHandle":"Bolt","PumpGrip":"Pump","MagazineGrip":"Magazine"}
    binds = []
    for name, at in anchors.items():
        parent = parents.get(name, "corps")
        local = at - anchors[parent] if name != "corps" else at
        node = {"name":name, "translation":local.tolist()}
        children = [joint_ids[n] for n in anchors if n != name and parents.get(n,"corps")==name]
        if children: node["children"] = children
        doc["nodes"].append(node)
        inv = np.eye(4); inv[:3,3] = -at
        binds.append(inv.T.reshape(-1))
    doc["skins"].append({"name":weapon+"_rig","joints":list(joint_ids.values()),"skeleton":0,
                         "inverseBindMatrices":append(doc,binary,binds,"MAT4")})
    groups = {}
    for index,(obj,island,material) in enumerate(raw["names"]):
        body, finish = part_type(weapon, obj, int(island), material)
        if body: groups.setdefault((body, finish), []).extend(np.flatnonzero(raw["meta"] == index))
    if "Shell" in anchors:
        for finish, points, directions in reload_prop(anchors["Shell"]):
            groups.setdefault(("Shell", finish), []).extend(range(len(vertices), len(vertices)+len(points)))
            vertices = np.concatenate((vertices, np.array(points)))
            normals = np.concatenate((normals, np.array(directions)))
    material_ids = {}
    primitives = []
    for (body, finish), triangles in groups.items():
        if finish not in material_ids: material_ids[finish] = create_material(doc,finish)
        points = vertices[triangles]
        if finish in ("Scorpion","Enfield") or finish in SPAS12_MATERIALS.values():
            uv = coordinates[triangles]
        else:
            # Project tiles along each polygon's dominant axis, keeping grain lengthwise on the stock.
            n = abs(normals[triangles].mean(axis=1)); lateral = np.argmax(n,axis=1)
            uv = np.empty((*points.shape[:2],2), float)
            for axis,a,b in ((0,2,1),(1,0,2),(2,0,1)):
                pick = lateral == axis
                uv[pick] = points[pick][:,:,[a,b]] * [3,6]
        rows = np.column_stack((points.reshape(-1,3),normals[triangles].reshape(-1,3),uv.reshape(-1,2))).astype("float32")
        rows, indices = np.unique(rows, axis=0, return_inverse=True)
        joint = np.zeros((len(rows),4), np.uint16); joint[:,0] = joint_ids[body]
        weight = np.zeros((len(rows),4), np.float32); weight[:,0] = 1
        attrs = {"POSITION":append(doc,binary,rows[:,:3],"VEC3",target=34962),
                 "NORMAL":append(doc,binary,rows[:,3:6],"VEC3",target=34962),
                 "TEXCOORD_0":append(doc,binary,rows[:,6:],"VEC2",target=34962),
                 "JOINTS_0":append(doc,binary,joint,"VEC4",5123,34962),
                 "WEIGHTS_0":append(doc,binary,weight,"VEC4",target=34962)}
        primitives.append({"attributes":attrs,"indices":append(doc,binary,indices.reshape(-1,1),"SCALAR",5123,34963),
                           "material":material_ids[finish]})
    doc["meshes"] = [{"name":weapon,"primitives":primitives}]
    doc["scenes"][0]["nodes"].append(len(doc["nodes"]))
    doc["nodes"].append({"name":weapon,"mesh":0,"skin":0})
    doc["extras"]={"svmap":{"mesh":"OpenScctRifles."+weapon}}
    write(output/(weapon+".glb"),doc,binary)
    return {"triangles":sum(len(i) for i in groups.values()),"muzzle":float(anchors["FX"][0]),"markers":{n:p.round(5).tolist() for n,p in anchors.items()}}


manifest = output / 'models.json'
models = json.loads(manifest.read_text()) if args.weapon and manifest.is_file() else {}
for name in ([args.weapon] if args.weapon else GUNS):
    models[name] = build(name)
    if name == "pumpshotgun":
        render_icon(output / (name + ".glb"), maps / (name + "_icon.png"))
manifest.write_text(json.dumps(models, indent=2) + '\n')
