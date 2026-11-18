"""Reads the tables of the light parity measurement (-ScctLightParity, Source/OpenSCCT/Test/ScctLightParitySubsystem.h)
and says how far Unreal's picture agrees with the original's light and dark.

    python Tools/Test/light_parity_report.py <folder or .csv> [...] [--shots <folder>] [--worst N]

For each table: the rank correlation between the simulation's light level and what was measured, the picture of the
spots the original has dark and of those it has lit, how often a lit spot is brighter in the picture than a dark one,
the pairs across a step in the level, what the exposure did, and the worst disagreements both ways. With --shots the
pictures of the worst disagreements are copied there.

Levels: "sim" is the luminance of the original's pawn light term at the spot (0 black, 1 a white texture at full white,
at most 2), lit from every side; "facing" the same on the side towards the camera. Pictures are display luma of 255.
The grey sphere has an albedo of 0.18, which the original would draw as a texture of display value 0.46: its picture in
the original would be about 255 x min(1, 0.46 x facing).
"""
import argparse
import csv
import math
import os
import shutil
import statistics

DARK = 0.15   # the bots' "lit" threshold for a spy (docs/AI.md): below it the original draws a pawn near black
LIT = 0.6     # a pawn the original draws clearly lit
SPHERE_DISPLAY = 0.46


def ranks(values):
    order = sorted(range(len(values)), key=lambda i: values[i])
    out = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and values[order[j + 1]] == values[order[i]]:
            j += 1
        for k in range(i, j + 1):
            out[order[k]] = (i + j) / 2.0 + 1.0
        i = j + 1
    return out


def spearman(a, b):
    pairs = [(x, y) for x, y in zip(a, b) if x is not None and y is not None and y >= 0]
    if len(pairs) < 3:
        return float("nan"), len(pairs)
    ra, rb = ranks([p[0] for p in pairs]), ranks([p[1] for p in pairs])
    ma, mb = statistics.fmean(ra), statistics.fmean(rb)
    num = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    den = math.sqrt(sum((x - ma) ** 2 for x in ra) * sum((y - mb) ** 2 for y in rb))
    return (num / den if den > 0 else float("nan")), len(pairs)


def ordered_share(low, high):
    """How often a value of high is above a value of low (ties count half): 1 perfect, 0.5 chance."""
    if not low or not high:
        return float("nan")
    wins = sum(1.0 if h > l else 0.5 if h == l else 0.0 for l in low for h in high)
    return wins / (len(low) * len(high))


def linear(display255):
    c = max(0.0, min(1.0, display255 / 255.0))
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def med(values):
    values = [v for v in values if v is not None and v >= 0]
    return statistics.median(values) if values else float("nan")


def span(values):
    values = [v for v in values if v is not None and v >= 0]
    return "%.0f (%.0f..%.0f, n=%d)" % (statistics.median(values), min(values), max(values), len(values)) if values else "-"


def read(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k, v in list(r.items()):
            if k not in ("name", "kind"):
                r[k] = float(v)
        r["baked_known"] = r["baked"] if r["has_baked"] else None
        # The picture taken back to light and divided by the exposure: the scene's light as the game's own view had it,
        # but for the tone curve (which only bends the ends).
        for part, exposure in (("sphere", "sphere_exposure"), ("ground", "sphere_exposure"), ("body", "body_exposure"), ("behind", "body_exposure")):
            r[part + "_light"] = linear(r[part + "_pic"]) / r[exposure] if r[part + "_pic"] >= 0 and r[exposure] > 0 else -1.0
        r["file"] = os.path.splitext(path)[0]
    return rows


def report(path, shots, worst):
    rows = read(path)
    if not rows:
        return
    name = rows[0]["name"]
    col = lambda k: [r[k] for r in rows]
    print("=== %s: %d spots (%s)" % (name, len(rows), ", ".join("%d %s" % (sum(1 for r in rows if r["kind"] == k), k)
          for k in ("dark", "bright", "spread", "edge-lit", "edge-dark"))))

    print(" rank correlation (Spearman) with the simulation's level:")
    for label, a, b in (
            ("sim    vs sphere in the picture", "sim", "sphere_pic"),
            ("facing vs sphere in the picture", "sim_facing", "sphere_pic"),
            ("sim    vs sphere, picture as light / exposure", "sim", "sphere_light"),
            ("facing vs sphere, picture as light / exposure", "sim_facing", "sphere_light"),
            ("sim    vs sphere in the HDR capture", "sim", "sphere_raw"),
            ("sim    vs body in the picture", "sim", "body_pic"),
            ("facing vs body in the picture", "sim_facing", "body_pic"),
            ("facing vs body, picture as light / exposure", "sim_facing", "body_light"),
            ("baked floor vs surroundings in the picture", "baked_known", "ground_pic"),
            ("baked floor vs surroundings, as light / exposure", "baked_known", "ground_light")):
        rho, n = spearman(col(a), col(b))
        print("   %-46s %+.2f (n=%d)" % (label, rho, n))

    dark = [r for r in rows if r["sim"] < DARK]
    lit = [r for r in rows if r["sim_facing"] >= LIT]
    for label, group in (("dark by the original (sim < %.2f)" % DARK, dark), ("lit by the original (facing >= %.1f)" % LIT, lit)):
        if not group:
            print(" %s: none" % label)
            continue
        g = lambda k: [r[k] for r in group]
        print(" %s, picture of 255, median (range):" % label)
        print("   sphere %s  its surroundings %s" % (span(g("sphere_pic")), span(g("ground_pic"))))
        print("   body   %s  behind it        %s" % (span(g("body_pic")), span(g("behind_pic"))))
        steps = [abs(r["body_pic"] - r["behind_pic"]) for r in group if r["body_pic"] >= 0 and r["behind_pic"] >= 0]
        ratio = [(r["body_pic"] - r["behind_pic"]) / max(r["behind_pic"], r["body_pic"], 1.0) for r in group if r["body_pic"] >= 0 and r["behind_pic"] >= 0]
        if steps:
            print("   body against what is behind it: |difference| median %.0f of 255 (%.0f..%.0f); (body - behind) / brighter median %+.2f; "
                  "within 8 of 255 at %d of %d spots" % (statistics.median(steps), min(steps), max(steps), statistics.median(ratio),
                                                        sum(1 for s in steps if s <= 8), len(steps)))
        print("   before exposure (picture as light / exposure): sphere median %.5f, body %.5f; in the HDR capture %.5f, %.5f; exposure scale median %.2f (%.2f..%.2f)" % (
            med(g("sphere_light")), med(g("body_light")), med(g("sphere_raw")), med(g("body_raw")),
            med(g("sphere_exposure")), min(g("sphere_exposure")), max(g("sphere_exposure"))))
        known = [r for r in group if r["has_baked"]]
        if known:
            print("   where the floor is lightmapped (n=%d): the original draws a mid-grey floor at %.0f of 255 (median of 127.5 x baked), Unreal's surroundings are at %.0f" % (
                len(known), statistics.median(min(255.0, 127.5 * r["baked"]) for r in known), med([r["ground_pic"] for r in known])))
        expected = [255.0 * min(1.0, SPHERE_DISPLAY * r["sim_facing"]) for r in group]
        print("   the original would draw the sphere at about %.0f of 255 (median of 255 x min(1, 0.46 x facing))" % statistics.median(expected))

    if dark and lit:
        print(" a lit spot is brighter than a dark one in %.0f %% of pairs by the sphere's picture, %.0f %% before exposure, %.0f %% by the body's picture" % (
            100 * ordered_share([r["sphere_pic"] for r in dark], [r["sphere_pic"] for r in lit]),
            100 * ordered_share([r["sphere_light"] for r in dark], [r["sphere_light"] for r in lit]),
            100 * ordered_share([r["body_pic"] for r in dark if r["body_pic"] >= 0], [r["body_pic"] for r in lit if r["body_pic"] >= 0])))
        raw_ratio = med([r["sphere_light"] for r in lit]) / max(med([r["sphere_light"] for r in dark]), 1e-6)
        pic_ratio = linear(med([r["sphere_pic"] for r in lit])) / max(linear(med([r["sphere_pic"] for r in dark])), 1e-6)
        print(" lit over dark, sphere medians: x%.1f before exposure, x%.1f in the picture (as light), %.0f against %.0f of 255" % (
            raw_ratio, pic_ratio, med([r["sphere_pic"] for r in lit]), med([r["sphere_pic"] for r in dark])))

    pairs = [(rows[i], rows[i + 1]) for i in range(len(rows) - 1) if rows[i]["kind"] == "edge-lit" and rows[i + 1]["kind"] == "edge-dark"]
    if pairs:
        print(" steps in the level (two spots about 2 m apart with an open line between them):")
        for a, b in pairs:
            print("   %02d/%02d sim %.2f / %.2f: sphere %3.0f / %3.0f of 255, before exposure %.4f / %.4f, body %3.0f / %3.0f%s" % (
                a["index"], b["index"], a["sim"], b["sim"], a["sphere_pic"], b["sphere_pic"], a["sphere_light"], b["sphere_light"], a["body_pic"], b["body_pic"],
                "" if a["sphere_pic"] > b["sphere_pic"] else "   <- the dark side is not darker"))

    rho, n = spearman(col("sphere_scene"), col("sphere_exposure"))
    print(" exposure: scale %.2f..%.2f (EV100 %.1f..%.1f); rank correlation of the scale with the view's average luminance %+.2f" % (
        min(col("sphere_exposure")), max(col("sphere_exposure")), math.log2(1 / (1.2 * max(col("sphere_exposure")))),
        math.log2(1 / (1.2 * min(col("sphere_exposure")))), rho))

    too_lit = sorted(dark, key=lambda r: -r["sphere_pic"])[:worst]
    too_dark = sorted([r for r in rows if r["sim_facing"] >= LIT], key=lambda r: r["sphere_pic"])[:worst]
    for label, group in (("dark by the original but lit in Unreal", too_lit), ("lit by the original but dark in Unreal", too_dark)):
        print(" worst, %s:" % label)
        for r in group:
            print("   %s_%02d %-9s at %.0f,%.0f,%.0f sim %.2f facing %.2f baked %s: sphere %.0f, surroundings %.0f, body %.0f, behind %.0f of 255; exposure %.2f" % (
                name, r["index"], r["kind"], r["x"], r["y"], r["z"], r["sim"], r["sim_facing"],
                "%.2f" % r["baked"] if r["has_baked"] else "-", r["sphere_pic"], r["ground_pic"], r["body_pic"], r["behind_pic"], r["sphere_exposure"]))
            if shots:
                os.makedirs(shots, exist_ok=True)
                for part in ("sphere", "body"):
                    source = "%s_%02d_%s.png" % (r["file"], r["index"], part)
                    if os.path.exists(source):
                        shutil.copy2(source, os.path.join(shots, "%s_%s_%02d_%s.png" % (
                            name, "dark-shown-lit" if group is too_lit else "lit-shown-dark", r["index"], part)))
    print()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tables", nargs="+", help="a .csv of the measurement, or a folder of them")
    parser.add_argument("--shots", help="copy the pictures of the worst disagreements to this folder")
    parser.add_argument("--worst", type=int, default=3, help="how many disagreements to list each way (default 3)")
    args = parser.parse_args()
    for entry in args.tables:
        files = sorted(os.path.join(entry, f) for f in os.listdir(entry) if f.endswith(".csv")) if os.path.isdir(entry) else [entry]
        for path in files:
            report(path, args.shots, args.worst)


if __name__ == "__main__":
    main()
