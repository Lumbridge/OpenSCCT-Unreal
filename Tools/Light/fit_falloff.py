"""The lighting plan's light units: the strength and falloff exponent an Unreal light gets so the original's display
term keeps its lit and its dark edge, and how far off the fit is elsewhere.

    python Tools/Light/fit_falloff.py                           the table of the fit for a row of peak terms
    python Tools/Light/fit_falloff.py <Map>_lights.csv [...]    every light of a map (the parity tool's light table),
                                                                with the bright-light edge check
    options: --cap 2.5 (scct.Light.PeakCap)

This is the same sum as FScctLightRules::Fit (Source/OpenSCCT/Play/ScctLightRules.cpp); change both together.

THE ORIGINAL. A light of peak term c (display units: 1 is a white texture at full white; Gain x colour in the parity
tool's light table) shows a surface that faces it, at the share x = d / R of its reach, at
    min(2, c x S(x)),  S(x) = 1 - 3x^2 + 2x^3       an ordinary light (the actor term; docs/RENDERING.md)
    min(2, c x (1 - x))                              a projector with linear attenuation, along its axis
THE ENGINE. A light without the inverse square law gives a surface of albedo A the scene light
    A x intensity x (1 - x^2)^exponent / pi          (measured by -ScctLightEngineChecks: 0.304 to 0.323 x intensity)
and under the plan's fixed exposure and tone curve scene light L shows as the display value L^(1 / 2.2).
THE FIT. Two numbers per light, so two display values keep their distance exactly: 0.5 (where a surface stops looking
lit) and 0.15 (where it starts looking dark; a light whose peak is under 0.83 keeps 0.6 and 0.18 of its peak). A light
whose strength would show more than the cap at the light (the original cut its term at 2) is held at the cap and keeps
the distance of its lit edge alone.
"""
import argparse
import csv
import math

LIT, DARK = 0.5, 0.15
SMOOTH, LINEAR, FLAT, LATE = 0, 1, 2, 3


def falloff(kind, x):
    x = min(max(x, 0.0), 1.0)
    if kind == LINEAR:
        return 1.0 - x
    if kind == FLAT:
        return 1.0
    if kind == LATE:
        return 1.0 if x <= 0.9 else (1.0 - x) / 0.1
    return 1.0 - 3.0 * x * x + 2.0 * x ** 3


def reach(kind, level):
    near, far = 0.0, 1.0
    for _ in range(48):
        middle = 0.5 * (near + far)
        if falloff(kind, middle) > level:
            near = middle
        else:
            far = middle
    return 0.5 * (near + far)


def fit(peak, kind=SMOOTH, cap=2.5):
    """(light at the light as scene light on a white surface, exponent, capped)."""
    if peak <= 1e-4:
        return 0.0, 2.2, False
    most = max(cap, 0.1) ** 2.2
    lit = min(LIT, 0.6 * peak)
    dark = lit * DARK / LIT
    capped = False
    if kind == FLAT:
        at_light, exponent = peak ** 2.2, 0.1
    else:
        lit_left = max(1.0 - reach(kind, lit / peak) ** 2, 1e-4)
        dark_left = min(max(1.0 - reach(kind, dark / peak) ** 2, 1e-5), lit_left * 0.999)
        exponent = 2.2 * math.log(lit / dark) / math.log(lit_left / dark_left)
        at_light = lit ** 2.2 / lit_left ** exponent
        if at_light > most and lit_left < 0.999:
            exponent = math.log(lit ** 2.2 / most) / math.log(lit_left)
            capped = True
    if at_light > most:
        at_light, capped = most, True
    return at_light, min(max(exponent, 0.1), 16.0), capped


def shown(at_light, exponent, x):
    """The display value the engine's light gives a white surface facing it at the share x of the reach."""
    return (at_light * max(1.0 - x * x, 0.0) ** exponent) ** (1.0 / 2.2)


def edge(at_light, exponent, level):
    """The share of the reach at which the engine's light shows `level`; None when it never does."""
    if shown(at_light, exponent, 0.0) <= level:
        return None
    near, far = 0.0, 1.0
    for _ in range(48):
        middle = 0.5 * (near + far)
        if shown(at_light, exponent, middle) > level:
            near = middle
        else:
            far = middle
    return 0.5 * (near + far)


def check(peak, kind, cap):
    """The fit and what it leaves: (at_light, exponent, capped, lit edge original, lit edge fitted, dark edge original,
    dark edge fitted, largest difference of display value over the reach against min(2, term))."""
    at_light, exponent, capped = fit(peak, kind, cap)
    original_lit = reach(kind, LIT / peak) if peak > LIT else None
    original_dark = reach(kind, DARK / peak) if peak > DARK else None
    worst = max(abs(shown(at_light, exponent, i / 200.0) - min(2.0, peak * falloff(kind, i / 200.0))) for i in range(201)) if peak > 0 else 0.0
    return (at_light, exponent, capped, original_lit, edge(at_light, exponent, LIT), original_dark, edge(at_light, exponent, DARK), worst)


def show(value):
    return "  -  " if value is None else "%.3f" % value


def table(cap):
    print("peak term -> intensity (x pi of the light at the light), exponent; where the display values 0.5 and 0.15 lie (share of the reach), original / fitted; largest difference of display value anywhere")
    for kind, name in ((SMOOTH, "ordinary light"), (LINEAR, "projector, linear attenuation")):
        print(" " + name)
        for peak in (0.1, 0.25, 0.5, 0.8, 1.0, 1.5, 2.0, 2.5, 3.2, 4.0, 5.9, 9.6):
            at_light, exponent, capped, lit0, lit1, dark0, dark1, worst = check(peak, kind, cap)
            print("   %5.2f -> %8.3f, %5.2f%s   lit %s / %s   dark %s / %s   worst %.2f" % (
                peak, at_light * math.pi, exponent, " capped" if capped else "       ", show(lit0), show(lit1), show(dark0), show(dark1), worst))


def lights(path, cap):
    rows = list(csv.DictReader(open(path, newline="")))
    used, bright, lit_off, dark_off, bright_off = 0, 0, [], [], []
    capped = 0
    for r in rows:
        flags, cls = int(r["flags"]), int(r["class"])
        if flags & 256 or cls in (0, 5):
            continue
        peak = float(r["gain"]) * max(float(r["r"]), float(r["g"]), float(r["b"]))
        if peak <= 0:
            continue
        kind = SMOOTH
        if cls == 1:
            kind = {0: FLAT, 2: LATE}.get(int(r["attenuation"]), LINEAR)
        at_light, exponent, was_capped, lit0, lit1, dark0, dark1, worst = check(peak, kind, cap)
        used += 1
        capped += 1 if was_capped else 0
        if lit0 and lit1:
            lit_off.append(abs(lit1 - lit0))
            if float(r["brightness"]) > 465:
                bright += 1
                bright_off.append(abs(lit1 / lit0 - 1.0))
        if dark0 and dark1:
            dark_off.append(abs(dark1 - dark0))
    print("%s: %d lights with a strength, %d held at the cap %.2f" % (path, used, capped, cap))
    if lit_off:
        print("   the 0.5 edge is at most %.1f %% of the light's reach off the original's (n=%d); the 0.15 edge at most %.1f %%, median %.2f %% (n=%d)" % (
            100 * max(lit_off), len(lit_off), 100 * max(dark_off), 100 * sorted(dark_off)[len(dark_off) // 2], len(dark_off)))
    if bright_off:
        print("   %s bright-light edge (brightness over 465, n=%d): the 0.5 edge is at most %.2f %% off the original's distance (target within 10 %%)" % (
            "ok  " if max(bright_off) <= 0.10 else "MISS", bright, 100 * max(bright_off)))
    else:
        print("   -    bright-light edge: no light with a brightness over 465")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tables", nargs="*")
    parser.add_argument("--cap", type=float, default=2.5)
    args = parser.parse_args()
    if not args.tables:
        table(args.cap)
    for path in args.tables:
        lights(path, args.cap)


if __name__ == "__main__":
    main()
