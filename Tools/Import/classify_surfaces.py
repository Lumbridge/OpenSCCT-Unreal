"""Sorts every original material of the given maps into a surface class and writes surface_classes.json.

Run with the system Python (needs numpy and Pillow):

    python -I classify_surfaces.py <content root> <Map> [<Map> ...]

Three things decide a material's class, most trusted first:

  1. the words in its name (the artists named most textures after what they show, in French or English),
  2. the SurfaceType the original game gave it for footsteps and bullet impacts (Core.Object.ESurfaceType),
  3. the picture itself: how colourful and how bright it is tells painted from bare.

The result is a data file meant to be read and corrected by hand: put a correction under "overrides" (material
source name -> class) and it wins, and survives the next run of this script. "classes" holds the look of each class
and is never rewritten here; only "materials" is regenerated.
"""
import colorsys
import json
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "surface_classes.json")

# Core.Object.ESurfaceType, by number.
SURFACE_TYPES = [
    "Generic", "Carpet", "RoofCanevas", "SnowPowder", "Grass", "ConcreteHard", "ConcreteDirt", "WoodHard",
    "WoodBoomy", "RoofTile", "Conveyor", "Scaffolding", "WaterPuddle", "Snow", "MetalHard", "DeepWater",
    "MetalReverb", "MetalSheet", "MediumWater", "BreakingGlass", "Gravel", "Sand", "FenceMetal", "FenceRope",
    "FenceVine", "FakeCeiling", "SteamPipe", "WaterPipe",
]

# Name words, checked in order: the first match wins. Words are matched inside the material's own name (the last
# part of "Package.Group.Name"), lower case. French first: most of these maps were made in Annecy and Milan.
NAME_RULES = [
    ("screen", r"ecran|screen|ordi(?!n)|comput|monitor|moniteur|televis|(?<![a-z])tv(?![a-z])|laptop|radar|display|hack_|borne"),
    ("water", r"water|(?<![a-z])eau(?![a-z])|flotte|liquid|ocean|(?<![a-z])mer(?![a-z])|wave|vague"),
    ("glass", r"glass|verre|vitre|vitrine|window|fenetre|windo|miroir|mirror|bouteille|bottle|ampoule|bulb|cristal|crystal"),
    ("foliage", r"kelp|algue|plant|leaf|feuill|lierre|ivy|arbre|tree|branch|buisson|bush|fern|palm|herbe(?!.*sol)|sapin"),
    ("snow", r"snow|neige"),
    ("ice", r"(?<![a-z])ice(?![a-z])|glace|givre"),
    ("grass", r"grass|gazon|pelouse|herbe"),
    ("sand", r"sand|sable"),
    ("gravel", r"gravier|gravel|caillou|pebble"),
    ("ground", r"terre|(?<!metal)dirt|(?<![a-z])mud|boue|(?<!back)ground|earth|soil"),
    ("asphalt", r"asphalt|bitume|goudron|(?<![a-z])road|route|macadam|tarmac|trottoir|sidewalk|pavement"),
    ("rock", r"rock|roche|rocher|cliff|falaise|cave(?!au)|grotte|montagne|mountain"),
    ("brick", r"brick|brique|briq"),
    ("tiles", r"tile|carrel|carreau|ceram|faience|mosai|dalle|damier"),
    ("marble", r"marble|marbre|granit|polish"),
    ("stone", r"stone|pierre|pave(?!m)|cobble|temple|colonne|column|statue|sculpt"),
    ("rust", r"rust|rouille|rouil|corro"),
    ("grating", r"grille|grillage|grid|grating|grate|fence|mesh|caillebotis|treillis|barbele|barreau"),
    ("metal", r"metal|steel|acier|(?<![a-z])fer(?![a-z])|iron|(?<![a-z])alu|chrome|inox|tuyau|pipe|vent(?!e)|aerat|"
              r"rail|poutre|beam|girder|tole|boulon|bolt|rivet|chain|chaine|cable|locker|casier|armoire|container|"
              r"conteneur|baril|barrel|bidon|tank|cuve|machin|engine|moteur|piston|valve|vanne|radiat|ladder|echelle|"
              r"lift|ascens|elevator|shutter|volet|hangar|gun|arme|turret|tourelle|trash|poubelle"),
    ("leather", r"leather|cuir|canape|sofa|fauteuil|armchair|couch"),
    ("carpet", r"moquette|carpet|tapis(?!s)|rug(?![a-z])"),
    ("fabric", r"fabric|tissu|cloth|rideau|curtain|drap|flag|drapeau|toile|canvas|bache|tarp|matelas|mattress|"
               r"coussin|cushion|pillow|oreiller|linge|towel|serviette|sac(?![a-z])|bag(?![a-z])"),
    ("paper", r"poster|affiche|paper|papier|carton|cardboard|book|livre|journal|magazine|photo|picture|tableau|"
              r"painting|peinture|sign(?![a-z])|panneau|plaque|label|etiquet|tag(?![a-z])|graff|(?<![a-z])map(?![a-z0-9])|dvd|"
              r"(?<![a-z])cd(?![a-z])|boite|(?<!sky)box(?![a-z])"),
    ("wood", r"wood|bois|parquet|plank|planche|lambris|crate|caisse|palette|pallet|tronc|trunk|bambo|bamboo|table|"
             r"desk|bureau|chair|chaise|banc(?![a-z])|bench|shelf|etagere|meuble|cabinet|door|porte|barrique|tonneau"),
    ("plastic", r"plast|rubber|caoutch|pneu|tire(?![a-z])|tyre|vinyl|lino|keyboard|clavier|phone|telephone|"
                r"camera|cam(?![a-z])|switch|interrupt|prise|button|bouton|extinct|helmet|casque|cone(?![a-z])"),
    ("plaster", r"plaster|platre|plafond|ceiling|crepi|stucco|tapiss|wallpaper|papier_?peint"),
    ("concrete", r"beton|concrete|ciment|cement|parpaing|bunker|(?<![a-z])wall|(?<![a-z])mur(?![a-z])|floor|"
                 r"(?<![a-z])sol(?![a-z])|pilier|pillar|stair|escal|marche"),
    ("lamp", r"light|lamp|lum(?![a-z])|neon|spot(?![a-z])|lustre|phare|lantern|lanterne|projo|projecteur|bulb"),
]

# What a SurfaceType says when the name says nothing.
TYPE_CLASS = {
    "Carpet": "fabric", "RoofCanevas": "fabric", "SnowPowder": "snow", "Snow": "snow", "Grass": "grass",
    "ConcreteHard": "concrete", "ConcreteDirt": "concrete_dirty", "WoodHard": "wood", "WoodBoomy": "wood",
    "RoofTile": "stone", "Conveyor": "plastic", "Scaffolding": "metal", "MetalHard": "metal",
    "MetalReverb": "metal", "MetalSheet": "metal", "BreakingGlass": "glossy", "Gravel": "gravel", "Sand": "sand",
    "FenceMetal": "grating", "FenceRope": "fabric", "FenceVine": "foliage", "FakeCeiling": "plaster",
    "SteamPipe": "metal", "WaterPipe": "metal", "WaterPuddle": "water", "DeepWater": "water",
    "MediumWater": "water",
}

# Families: a name class that agrees with the SurfaceType's family is trusted outright; one that disagrees is
# trusted only when the name word is a strong one (a "door" that sounds like metal is a metal door).
METAL_TYPES = {"MetalHard", "MetalReverb", "MetalSheet", "Scaffolding", "FenceMetal", "SteamPipe", "WaterPipe"}
WOOD_TYPES = {"WoodHard", "WoodBoomy"}
MINERAL_TYPES = {"ConcreteHard", "ConcreteDirt", "RoofTile", "Gravel", "FakeCeiling"}
WEAK_NAME_CLASSES = {"wood", "metal", "concrete", "plastic", "paper", "lamp", "stone", "leather", "plaster"}


def picture_stats(path):
    """Mean brightness and saturation, how much the brightness varies, and how much of the picture is see-through."""
    try:
        image = Image.open(path).convert("RGBA")
    except Exception:  # pylint: disable=broad-except
        return None
    image.thumbnail((64, 64))
    pixels = np.asarray(image, dtype=np.float32) / 255.0
    rgb, alpha = pixels[..., :3], pixels[..., 3]
    brightest, darkest = rgb.max(axis=-1), rgb.min(axis=-1)
    saturation = np.where(brightest > 0.02, (brightest - darkest) / np.maximum(brightest, 1e-3), 0.0)
    mean = rgb.reshape(-1, 3).mean(axis=0)
    hue = colorsys.rgb_to_hsv(float(mean[0]), float(mean[1]), float(mean[2]))[0]
    luma = rgb @ np.array([0.299, 0.587, 0.114])
    return {
        "brightness": float(luma.mean()),
        "contrast": float(luma.std()),
        "saturation": float(saturation.mean()),
        "hue": hue,
        "clear": float((alpha < 0.5).mean()),
    }


def classify(source, surface_type, flags, stats):
    """Returns (class, why)."""
    name = source.rsplit(".", 1)[-1].lower()
    surface = SURFACE_TYPES[surface_type] if surface_type is not None and surface_type < len(SURFACE_TYPES) else None
    stats = stats or {"brightness": 0.4, "contrast": 0.1, "saturation": 0.2, "hue": 0.0, "clear": 0.0}
    saturated = stats["saturation"] > 0.32

    from_name = None
    for cls, pattern in NAME_RULES:
        if re.search(pattern, name):
            from_name = cls
            break
    from_type = TYPE_CLASS.get(surface)

    # Projected textures, glows and other effects are not surfaces.
    if flags.get("projector") or source.startswith("PROJ."):
        return "effect", "projector"

    cls, why = from_name or from_type or "generic", "name" if from_name else "surface type" if from_type else "fallback"
    if from_name and from_type and from_name in WEAK_NAME_CLASSES:
        # The game's own word for what the surface is made of beats a weak name word.
        if surface in METAL_TYPES and from_name not in ("metal", "lamp"):
            cls, why = ("grating" if surface == "FenceMetal" else "metal"), "surface type over name"
        elif surface in WOOD_TYPES and from_name != "wood":
            cls, why = "wood", "surface type over name"
        elif surface in MINERAL_TYPES and from_name in ("wood", "metal", "plastic", "leather"):
            cls, why = from_type, "surface type over name"
        elif surface == "Carpet" and from_name in ("concrete", "plaster"):
            # The Carpet type is the game's "soft and quiet": a floor of it is carpet, a wall of it is padded.
            cls, why = ("carpet" if re.search(r"floor|sol(?![a-z])", name) else "fabric"), "surface type over name"

    # Glass and water must really be see-through or marked as such: a floor that only SOUNDS like water (the
    # sand at the bottom of a tank) stays a solid surface.
    if cls == "water" and from_name != "water" and not flags.get("translucent"):
        cls, why = "glossy", "water surface type on a solid surface"
    if cls == "glossy":
        if flags.get("translucent"):
            cls, why = "glass", "translucent breaking glass"
        elif flags.get("self_illum") or flags.get("unlit"):
            cls, why = "lamp", "self-lit breaking glass"
    if cls == "glass" and not (flags.get("translucent") or flags.get("env_map") or surface == "BreakingGlass"):
        cls, why = "glossy", "glass by name, drawn solid"
    if cls == "lamp" and not (flags.get("self_illum") or flags.get("unlit")):
        # A lamp housing: what the surface type says, or painted metal.
        cls, why = from_type if from_type not in (None, "glossy") else "metal", "lamp housing"
    if cls == "lamp" and flags.get("unlit") and (stats["brightness"] < 0.6 or stats["saturation"] > 0.45):
        # A lit picture, not a lamp's glass: an arcade screen, an advert, an exit sign, a lamp drawn together with
        # its housing. It glows like a screen; at a lamp's strength it would be a white rectangle.
        cls, why = "screen", "self-lit picture"
    if cls == "foliage" and not (flags.get("masked") or flags.get("translucent")):
        cls, why = from_type or "generic", "plant by name, drawn solid"
    if cls == "grass" and flags.get("masked"):
        cls, why = "foliage", "cut-out grass"
    if cls == "tiles" and (surface == "FakeCeiling" or re.search(r"plaf|cei(?![a-z])|cei_|ceiling", name)):
        cls, why = "plaster", "ceiling tiles are fibre board, not glazed"

    # The picture splits painted from bare.
    if cls == "metal":
        # Bare metal is grey, mid-toned and even. A colourful, very pale or busy picture on a metal object is paint,
        # enamel, a sign, a sticker or an instrument panel: shaded as a mirror-like metal those go dark and lose
        # their print, so they are painted metal.
        painted = saturated or stats["brightness"] > 0.7 or stats["contrast"] > 0.2
        cls = "metal_painted" if painted else "metal_bare"
    elif cls == "concrete":
        cls = "concrete_painted" if saturated or stats["brightness"] > 0.62 else "concrete_bare"
    elif cls == "plaster" and saturated:
        cls = "concrete_painted"
    elif cls == "wood" and re.search(r"parquet|desk|bureau|table|meuble|cabinet|armoire|piano|vernis|floor|sol", name):
        cls = "wood_finished"
    return cls, why


def write_document(document):
    """Written by hand so each class and each material is one line: easy to read, correct and diff."""
    def block(mapping):
        lines = ["  %s: %s" % (json.dumps(name), json.dumps(value)) for name, value in mapping.items()]
        return "{\n" + ",\n".join(lines) + "\n }" if lines else "{}"

    with open(OUT, "w", newline="\n") as f:
        f.write("{\n")
        f.write(' "about": %s,\n' % json.dumps(document["about"]))
        f.write(' "fallback": %s,\n' % json.dumps(document["fallback"]))
        f.write(' "classes": %s,\n' % block(document["classes"]))
        f.write(' "overrides": %s,\n' % block(document["overrides"]))
        f.write(' "materials": %s\n' % block(document["materials"]))
        f.write("}\n")


def main():
    content, maps = sys.argv[1], sys.argv[2:]
    existing = json.load(open(OUT)) if os.path.exists(OUT) else {}
    materials, reasons = {}, {}
    for map_name in maps:
        render = os.path.join(content, "svmap", map_name, "render")
        doc = json.load(open(os.path.join(render, "materials.json")))
        gameplay = doc.get("gameplay", {})
        for material_key, record in sorted(doc["materials"].items()):
            source = record.get("source") or material_key
            if source in materials:
                continue
            flags = record["flags"]
            texture = flags.get("texture")
            stats = picture_stats(os.path.join(render, texture)) if texture else None
            surface_type = gameplay.get(source, {}).get("SurfaceType")
            materials[source], reasons[source] = classify(source, surface_type, flags, stats)

    counts = {}
    for cls in materials.values():
        counts[cls] = counts.get(cls, 0) + 1
    for cls, count in sorted(counts.items(), key=lambda item: -item[1]):
        print("%5d  %s" % (count, cls))
    unknown = sorted(set(materials.values()) - set(existing.get("classes", {})))
    if unknown:
        print("classes without a look in surface_classes.json (they get the fallback): %s" % unknown)

    document = {
        "about": existing.get("about", ""),
        "fallback": existing.get("fallback", "generic"),
        "classes": existing.get("classes", {}),
        "overrides": existing.get("overrides", {}),
        "materials": dict(sorted(materials.items())),
    }
    write_document(document)
    # Why each one landed where it did, for review; not committed (it sits beside the import's other scratch).
    if os.environ.get("SCCT_CLASSIFY_REASONS"):
        with open(os.environ["SCCT_CLASSIFY_REASONS"], "w") as f:
            json.dump(reasons, f, indent=1)
    print("%d materials -> %s" % (len(materials), OUT))


if __name__ == "__main__":
    main()
