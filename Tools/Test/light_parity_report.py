"""Reads the tables of the light parity measurement (-ScctLightParity, Source/OpenSCCT/Test/ScctLightParitySubsystem.h)
and says how far Unreal's picture agrees with the original's light and dark.

    python Tools/Test/light_parity_report.py <folder or .csv> [...] [--shots <folder>] [--worst N]

For each table: the rank correlation between the simulation's light level and what was measured, the picture of the
spots the original has dark and of those it has lit, how often a lit spot is brighter in the picture than a dark one,
the pairs across a step in the level, what the exposure did, and the worst disagreements both ways. With --shots the
pictures of the worst disagreements are copied there. Then the switch test (<name>_switch.csv) and the tone ramp
(<name>_tone.csv) when the run made them, and last the lighting plan's targets with the table's figure against each
(the "TARGETS" block: what a step of the plan is judged by).

"pred" is the sphere's picture by the simulation's own pawn light term, per pixel over the measured disc (the bridge's
PredictSphere). The plan draws a body-class surface exactly so; the grey sphere of today is an ordinary lit mesh.

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


def percentile(values, share):
    values = sorted(values)
    if not values:
        return float("nan")
    at = share * (len(values) - 1)
    low = int(math.floor(at))
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (at - low)


def table(path):
    """The rows of a csv with every numeric field as a number."""
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k, v in list(r.items()):
            if k not in ("name", "kind", "light", "role"):
                r[k] = float(v)
    return rows


def read(path):
    rows = table(path)
    for r in rows:
        r.setdefault("pred", -1.0)
        r.setdefault("plan", 0.0)
        r.setdefault("play_floor", 0.0)
        r.setdefault("sphere_seen", 1.0)
        r.setdefault("body_seen", 1.0)
        if not r["body_seen"]:
            # The body was hidden from the camera: its reading is of what stands in the way.
            r["body_pic"] = r["behind_pic"] = r["body_raw"] = r["behind_raw"] = -1.0
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
    hidden = [r for r in rows if not r["sphere_seen"]]
    if hidden:
        print(" left out, the sphere was hidden from the camera (a door or a wall the simulation's trace did not stop at): %s" % ", ".join(
            "%s_%02d" % (name, r["index"]) for r in hidden))
    unseen_bodies = sum(1 for r in rows if r["sphere_seen"] and not r["body_seen"])
    if unseen_bodies:
        print(" %d more spots have no body reading (the body was hidden from the camera)" % unseen_bodies)
    all_rows = rows
    rows = [r for r in rows if r["sphere_seen"]]
    col = lambda k: [r[k] for r in rows]
    print("=== %s: %d spots (%s); scct.Light.Plan %d; %s" % (name, len(rows), ", ".join("%d %s" % (sum(1 for r in rows if r["kind"] == k), k)
          for k in ("dark", "bright", "spread", "edge-lit", "edge-dark")), rows[0]["plan"],
          "play floor only" if rows[0]["play_floor"] else "any connected floor under a map light"))

    print(" rank correlation (Spearman) with the simulation's level:")
    for label, a, b in (
            ("pred   vs sphere in the picture", "pred", "sphere_pic"),
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
        if a == "pred" and max(col(a)) < 0:
            continue  # a table from before the prediction was added
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

    pairs = [(all_rows[i], all_rows[i + 1]) for i in range(len(all_rows) - 1) if all_rows[i]["kind"] == "edge-lit" and all_rows[i + 1]["kind"] == "edge-dark"
             and all_rows[i]["sphere_seen"] and all_rows[i + 1]["sphere_seen"]]
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
    switches = switch_report(os.path.splitext(path)[0] + "_switch.csv")
    tone = tone_report(os.path.splitext(path)[0] + "_tone.csv")
    targets(rows, dark, lit, pairs, switches, tone)
    print()


def switch_report(path):
    """The switch test: the sphere near each switched light and beyond its radius, before, after one firing and after two."""
    rows = table(path)
    if not rows:
        print(" switch test: none in this run")
        return rows
    hidden = [r for r in rows if not r.get("seen", 1.0)]
    if hidden:
        print(" switch test, left out (sphere hidden from the camera): %s" % ", ".join("%s %s" % (r["light"], r["role"]) for r in hidden))
    rows = [r for r in rows if r.get("seen", 1.0)]
    print(" switch test (%d spots at %d switched lights): sphere of 255 measured / predicted by the simulation's term" % (
        len(rows), len(set(r["light"] for r in rows))))
    for r in rows:
        print("   %-22s %-4s was %-3s %4.0f uu of %4.0f: first %3.0f / %3.0f, fired %3.0f / %3.0f, back %3.0f / %3.0f   (sim %.2f -> %.2f -> %.2f)" % (
            r["light"], r["role"], "on" if r["on"] else "off", r["distance"], r["radius"], r["sphere_first"], r["pred_first"],
            r["sphere_fired"], r["pred_fired"], r["sphere_back"], r["pred_back"], r["sim_first"], r["sim_fired"], r["sim_back"]))
    return rows


def tone_report(path):
    """The tone ramp: the display value shown for a surface the original shows at a known one."""
    rows = table(path)
    if not rows:
        print(" tone ramp: none in this run")
        return rows
    print(" tone ramp (exposure scale %.2f): original display value -> shown (difference); an exact picture shows min(255, value)" % rows[0]["exposure"])
    # A few codes of disorder among the darkest patches are the picture's own doing (the glow and the lens ghosts of
    # the bright patches); a large drop is a wall or a prop in front of the board.
    if any(b["picture"] < a["picture"] - 20 for a, b in zip(rows, rows[1:])):
        print("   NOT VALID: a brighter patch reads far darker than the one before it, so something stands in front of the board (see <name>_tone.png)")
        for r in rows:
            r["cut"] = 1.0
    line = []
    for r in rows:
        line.append("%3.0f -> %5.1f (%+.0f)" % (r["code"], r["picture"], r["picture"] - min(255.0, r["code"])))
    for i in range(0, len(line), 6):
        print("   " + "   ".join(line[i:i + 6]))
    return rows


def targets(rows, dark, lit, pairs, switches, tone):
    """The lighting plan's targets (lighting-study synthesis) with this table's figure against each."""
    out = []

    def add(label, target, value, ok):
        out.append((label, target, value, "-" if ok is None else "ok" if ok else "MISS"))

    exposure = [r["sphere_exposure"] for r in rows]
    add("exposure scale", "1.00 on every spot", "%.2f..%.2f" % (min(exposure), max(exposure)), min(exposure) > 0.99 and max(exposure) < 1.01)
    if tone and tone[0].get("cut"):
        add("tone ramp", "24..200 within 2; 0..4 shows 0..4", "not valid (board cut)", None)
    elif tone:
        mid = [abs(r["picture"] - r["code"]) for r in tone if 24 <= r["code"] <= 200]
        low = [r["picture"] for r in tone if r["code"] <= 4]
        if mid:
            add("tone ramp 24..200", "within 2", "worst %.0f off" % max(mid), max(mid) <= 2)
        if low:
            add("tone ramp, original 0..4", "shows 0..4", "%.0f..%.0f" % (min(low), max(low)), max(low) <= 4.5)
    else:
        add("tone ramp", "24..200 within 2; 0..4 shows 0..4", "not measured", None)
    have = [r for r in rows if r["pred"] >= 0 and r["sphere_pic"] >= 0]
    if have:
        rho, n = spearman([r["pred"] for r in have], [r["sphere_pic"] for r in have])
        add("rank correlation, predicted vs measured sphere", ">= 0.95", "%+.2f (n=%d)" % (rho, n), rho >= 0.95)
    rho, n = spearman([r["sim"] for r in rows], [r["body_pic"] for r in rows])
    add("rank correlation, sim vs body", ">= 0.90", "%+.2f (n=%d)" % (rho, n), rho >= 0.90)
    if have:
        off = [abs(r["sphere_pic"] - r["pred"]) for r in have]
        add("sphere error against the prediction", "median <= 6, 95th <= 15", "median %.0f, 95th %.0f" % (statistics.median(off), percentile(off, 0.95)),
            statistics.median(off) <= 6 and percentile(off, 0.95) <= 15)
    black = [r["sphere_pic"] for r in rows if r["sim"] < 0.005 and r["sphere_pic"] >= 0]
    if black:
        add("dark spots, sim = 0: sphere", "median <= 2, worst <= 8", "median %.0f, worst %.0f (n=%d)" % (statistics.median(black), max(black), len(black)),
            statistics.median(black) <= 2 and max(black) <= 8)
    dim = [r["sphere_pic"] for r in dark if r["sphere_pic"] >= 0]
    if dim:
        add("dark spots, sim < 0.15: sphere", "<= 18", "worst %.0f (n=%d)" % (max(dim), len(dim)), max(dim) <= 18)
    bright = [r["sphere_pic"] for r in lit if r["sphere_pic"] >= 0]
    if bright:
        add("lit spots (facing >= 0.6): sphere", "median 95..125, none under 60", "median %.0f, least %.0f (n=%d)" % (statistics.median(bright), min(bright), len(bright)),
            95 <= statistics.median(bright) <= 125 and min(bright) >= 60)
    if dim and bright:
        share = 100 * ordered_share(dim, bright)
        add("lit over dark, all pairs", "100 % ordered", "%.0f %%" % share, share >= 99.95)
    if pairs:
        good = sum(1 for a, b in pairs if b["sphere_pic"] <= 8 or a["sphere_pic"] >= 4 * b["sphere_pic"])
        add("2 m step pairs", "lit >= 4x dark, or dark <= 8, on all", "%d of %d" % (good, len(pairs)), good == len(pairs))
    floors = [r for r in rows if r["has_baked"] and r["ground_pic"] >= 0]
    shown = [r for r in floors if 127.5 * r["baked"] > 4]
    if shown:
        ratio = statistics.median(r["ground_pic"] / min(255.0, 127.5 * r["baked"]) for r in shown)
        add("lightmapped floor against the original (mid-grey floor)", "ratio 0.85..1.2", "median ratio %.2f (n=%d)" % (ratio, len(shown)), 0.85 <= ratio <= 1.2)
    unlit = [r["ground_pic"] for r in floors if 127.5 * r["baked"] <= 4]
    if unlit:
        add("floor where the original is 0..4", "<= 8", "worst %.0f, median %.0f (n=%d)" % (max(unlit), statistics.median(unlit), len(unlit)), max(unlit) <= 8)
    rho, n = spearman([r["baked_known"] for r in rows], [r["ground_pic"] for r in rows])
    add("rank correlation, baked floor vs surroundings", ">= 0.90", "%+.2f (n=%d)" % (rho, n), rho >= 0.90)
    # A dark wall: what is behind the body is under the "lit" band of the targets (24 of 255).
    bodies = [r for r in dark if r["body_pic"] >= 0 and r["behind_pic"] >= 0]
    on_dark = [r for r in bodies if r["behind_pic"] < 24]
    on_lit = [r for r in bodies if r["behind_pic"] >= 24]
    if on_dark:
        near = sum(1 for r in on_dark if abs(r["body_pic"] - r["behind_pic"]) <= 4)
        add("body in the dark on a dark wall (behind < 24)", "within 4 of the background on >= 90 %", "%d of %d" % (near, len(on_dark)), near >= 0.9 * len(on_dark))
    if on_lit:
        worst = max(r["body_pic"] for r in on_lit)
        add("body in the dark on a lit wall (behind >= 24)", "body <= 8", "worst %.0f, median %.0f (n=%d)" % (
            worst, statistics.median(r["body_pic"] for r in on_lit), len(on_lit)), worst <= 8)
    if switches:
        near = [r for r in switches if r["role"] == "near"]
        far = [r for r in switches if r["role"] == "far"]
        if near:
            off = [abs(r["sphere_fired"] - r["pred_fired"]) for r in near]
            add("switch test: sphere after switching against the prediction", "within 6", "worst %.0f, median %.0f (n=%d)" % (max(off), statistics.median(off), len(off)), max(off) <= 6)
            # Only where the simulation itself is back as before: some switched lights of a map go out for good.
            returned = [r for r in near + far if abs(r["sim_back"] - r["sim_first"]) < 0.01 and abs(r["pred_back"] - r["pred_first"]) < 0.5]
            back = [abs(r["sphere_back"] - r["sphere_first"]) for r in returned]
            if back:
                add("switch test: back on against before", "within 5", "worst %.0f, median %.0f (n=%d; %d spots stay switched in the simulation)" % (
                    max(back), statistics.median(back), len(back), len(near + far) - len(back)), max(back) <= 5)
        if far:
            moved = [abs(r["sphere_fired"] - r["sphere_first"]) for r in far if abs(r["pred_fired"] - r["pred_first"]) < 0.5]
            if moved:
                add("switch test: beyond the radius", "change <= 2", "worst %.0f, median %.0f (n=%d)" % (max(moved), statistics.median(moved), len(moved)), max(moved) <= 2)
    else:
        add("switch test", "within 6 of the prediction; beyond the radius <= 2; back within 5", "not measured", None)
    print(" TARGETS of the lighting plan (picture of 255):")
    for label, target, value, verdict in out:
        print("   %-4s %-56s %-34s target %s" % (verdict, label, value, target))
    print("   %d of %d targets met" % (sum(1 for o in out if o[3] == "ok"), sum(1 for o in out if o[3] != "-")))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tables", nargs="+", help="a .csv of the measurement, or a folder of them")
    parser.add_argument("--shots", help="copy the pictures of the worst disagreements to this folder")
    parser.add_argument("--worst", type=int, default=3, help="how many disagreements to list each way (default 3)")
    args = parser.parse_args()
    for entry in args.tables:
        files = sorted(os.path.join(entry, f) for f in os.listdir(entry) if f.endswith(".csv") and not f.endswith(("_switch.csv", "_tone.csv"))) \
            if os.path.isdir(entry) else [entry]
        for path in files:
            report(path, args.shots, args.worst)


if __name__ == "__main__":
    main()
