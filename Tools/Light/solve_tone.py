"""The lighting plan's ramp solve: is the picture's tone curve the wanted one, and if not, what would make it so.

    python Tools/Light/solve_tone.py <Map>_tone.csv [more ...] [--knee 200] [--gate 1] [--gain 1] [--gamma 1] [--exposure 1]

The light parity tool (-ScctLightParity, Source/OpenSCCT/Test/ScctLightParityChecks.cpp) stands a board of unlit grey
patches in front of the camera, each glowing with the scene light of a surface the original shows at a known display
value (light = (value / 255) ^ 2.2, values 0 to 510: the original's light term reaches 2), and writes what the
finished picture shows for each to <Map>_tone.csv. This script reads those tables and

  - prints, for every patch, the wanted display value by the plan's curve (the same steps as the shader in
    Tools/Materials/build_tone_material.py: gain and gamma, the shoulder above the knee, the original's glow gate),
    the measured one and the difference;
  - checks the plan's targets: 24..200 within 2, and the original's 0..4 shown at 0..4;
  - fits the correction that would bring the measured ramp onto the wanted one between 24 and 200, as the two numbers
    the game takes: scct.Light.ToneGain and scct.Light.ToneGamma (the display value becomes Gain x value ^ Gamma). When
    the ramp is met they come out at the values the run was made with (--gain, --gamma; 1 and 1 by default).

The options say what the run was made with (the game's defaults when left out), so the wanted column is the curve the
game was asked for. Nothing here writes into the project: the two numbers go into ScctTone.cpp's defaults by hand, so a
change of the picture is a change someone made.
"""
import argparse
import csv
import math
import sys

GATE_SLOPE = 16.0
GATE_FLOOR = 0.03


def wanted(code, knee=200.0, gate=1.0, gain=1.0, gamma=1.0, exposure=1.0):
    """The display value (of 255) the plan's curve shows for a grey surface the original shows at code, at an exposure scale."""
    value = (math.pow(max(code, 0.0) / 255.0, 2.2) * exposure) ** (1.0 / 2.2)
    value = gain * math.pow(value, gamma)
    k = min(max(knee / 255.0, 0.05), 0.999)
    w = 1.0 - k
    value = min(value, k) + w * (1.0 - math.exp(-max(value - k, 0.0) / w))
    # A grey patch: its luminance is the value itself.
    value *= 1.0 + gate * (min(max(GATE_SLOPE * (value - GATE_FLOOR), 0.0), 1.0) - 1.0)
    return 255.0 * min(value, 1.0)


def read(path):
    with open(path, newline="") as handle:
        return [{key: (value if key == "name" else float(value)) for key, value in row.items()} for row in csv.DictReader(handle)]


def fit(rows, gain, gamma):
    """Least squares of log(measured) against log(code) between 24 and 200: the gain and gamma that would have met the ramp."""
    points = [(math.log(r["code"] / 255.0), math.log(max(r["picture"], 0.25) / 255.0)) for r in rows if 24 <= r["code"] <= 200]
    if len(points) < 3:
        return None
    n = len(points)
    mx = sum(x for x, _ in points) / n
    my = sum(y for _, y in points) / n
    sxx = sum((x - mx) ** 2 for x, _ in points)
    if sxx <= 0.0:
        return None
    slope = sum((x - mx) * (y - my) for x, y in points) / sxx
    offset = my - slope * mx
    if slope <= 0.0:
        return None
    # The picture shows a x (Gain x code ^ Gamma) ^ b for some a and b it should not have: slope = b x Gamma and
    # offset = log a + b log Gain. The Gain and Gamma that cancel a and b are Gain x exp(-offset x Gamma / slope) and
    # Gamma / slope.
    return gain * math.exp(-offset * gamma / slope), gamma / slope


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("tables", nargs="+")
    parser.add_argument("--knee", type=float, default=200.0)
    parser.add_argument("--gate", type=float, default=1.0)
    parser.add_argument("--gain", type=float, default=1.0)
    parser.add_argument("--gamma", type=float, default=1.0)
    parser.add_argument("--exposure", type=float, default=None, help="the exposure scale of the run (default: the table's own column)")
    arguments = parser.parse_args()

    met_all = True
    for path in arguments.tables:
        rows = read(path)
        if not rows:
            print("%s: empty" % path)
            met_all = False
            continue
        exposure = arguments.exposure if arguments.exposure is not None else rows[0].get("exposure", 1.0)
        print("%s (%s, scct.Light.Plan %d, exposure scale %.3f)" % (path, rows[0]["name"], int(rows[0].get("plan", 0)), exposure))
        print("  original   wanted  measured   off")
        worst_mid = 0.0
        most_low = 0.0
        for row in rows:
            want = wanted(row["code"], arguments.knee, arguments.gate, arguments.gain, arguments.gamma, exposure)
            off = row["picture"] - want
            mark = ""
            if 24 <= row["code"] <= 200:
                # The plan's target is against the original's value itself, which the curve is at an exposure of 1.
                worst_mid = max(worst_mid, abs(off))
                mark = "  <-- more than 2" if abs(off) > 2.0 else ""
            if row["code"] <= 4:
                most_low = max(most_low, row["picture"])
            print("  %8.0f %8.1f %9.1f %+6.1f%s" % (row["code"], want, row["picture"], off, mark))
        met = worst_mid <= 2.0 and most_low <= 4.5
        met_all = met_all and met
        print("  24..200: worst %.1f off the wanted curve (target 2). Original 0..4 shows at most %.1f (target 4). %s" % (
            worst_mid, most_low, "MET" if met else "NOT MET"))
        solved = fit(rows, arguments.gain, arguments.gamma)
        if solved is None:
            print("  no fit: fewer than three patches between 24 and 200")
        elif abs(exposure - 1.0) > 0.005:
            print("  no fit: the correction is solved at an exposure scale of 1 only")
        else:
            print("  fit between 24 and 200: scct.Light.ToneGain %.4f, scct.Light.ToneGamma %.4f (the run had %.4f and %.4f)" % (
                solved[0], solved[1], arguments.gain, arguments.gamma))
    return 0 if met_all else 1


if __name__ == "__main__":
    sys.exit(main())
