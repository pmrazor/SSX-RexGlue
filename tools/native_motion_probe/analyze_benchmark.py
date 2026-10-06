"""Summarize alternating native-motion GPU timestamp samples (milliseconds)."""
import argparse
import csv
import json
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    return {"count": len(values), "median_ms": statistics.median(values),
            "mean_ms": statistics.mean(values),
            "p95_ms": values[int((len(values) - 1) * .95)],
            "min_ms": values[0], "max_ms": values[-1]}


def analyze(path):
    with path.open(newline="") as file:
        rows = list(csv.DictReader(file))
    modes = {mode: distribution([float(row["native_ms"]) for row in rows
                                 if row["mode"] == mode])
             for mode in ("every_frame", "off")}
    rounds = []
    for number in sorted({int(row["round"]) for row in rows}):
        values = {mode: statistics.median(float(row["native_ms"]) for row in rows
                                          if int(row["round"]) == number and row["mode"] == mode)
                  for mode in modes}
        rounds.append({"round": number, **values,
                       "saved_ms": values["every_frame"] - values["off"]})
    return {"modes": modes, "rounds": rounds,
            "median_saved_ms": modes["every_frame"]["median_ms"] - modes["off"]["median_ms"],
            "scope": "Native-motion pass only, replaying fixed captured geometry/depth. "
                     "Excludes uploads, camera motion, DLAA and presentation. "
                     "Not a whole-game FPS benchmark. Normal sampled mode still "
                     "uses diagnostic atomics once every 120 frames."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()
    report = json.dumps(analyze(args.csv), indent=2) + "\n"
    args.csv.with_suffix(".json").write_text(report)
    print(report)
