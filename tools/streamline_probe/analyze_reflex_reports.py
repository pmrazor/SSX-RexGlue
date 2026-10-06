"""Join NVIDIA driver reports to completed SSX Reflex tokens in a local trace.

Usage: python analyze_reflex_reports.py timing.csv ssx.log
Writes timing.reflex-driver.json; preserves raw timestamps in the input log.
"""
import csv
import json
import re
import statistics
import sys
from pathlib import Path


PATTERN = re.compile(r"SSX_REFLEX_REPORT token=(\d+) sim=(\d+)\.\.(\d+) "
                     r"render=(\d+)\.\.(\d+) present=(\d+)\.\.(\d+) "
                     r"driver=(\d+)\.\.(\d+) queue=(\d+)\.\.(\d+) gpu=(\d+)\.\.(\d+)")


def analyze(trace, log):
    with trace.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    completed = {int(r["related"]) for r in rows if r["event"] == "reflex_present_end"}
    aborted = {int(r["related"]) for r in rows if r["event"] == "reflex_abort"}
    reports = {}
    # spdlog rotates a busy SSX session into ssx_NNN.1.log, .2.log, etc.
    logs = sorted(log.parent.glob(log.stem + ".[0-9]*" + log.suffix)) + [log]
    for source in logs:
        with source.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if match := PATTERN.search(line):
                    token, *times = map(int, match.groups())
                    reports[token] = times
    ordered, errors, gpu_ms = [], [], []
    for token in sorted(completed & reports.keys()):
        s0, s1, r0, r1, p0, p1, d0, d1, q0, q1, g0, g1 = reports[token]
        # Driver 617.14 may invert adjacent timestamps by 1 us. Never sort or
        # clamp the raw values; allow at most the probe's existing 2 us tolerance.
        pairs = [(s0, s1), (s1, r0), (r0, r1), (r1, p0), (p0, p1),
                 (d0, d1), (q0, q1), (g0, g1), (r0, g0)]
        if token in aborted or not all(reports[token]) or any(a > b + 2 for a, b in pairs):
            errors.append({"token": token, "raw_timestamps_us": reports[token]})
        else:
            ordered.append(token)
            gpu_ms.append((g1 - s0) / 1000)
    return {"completed_trace_tokens": len(completed), "matched_ordered_driver_reports": len(ordered),
            "missing_driver_token_ids": sorted(completed - reports.keys()),
            "errors": errors, "matched_token_ids": ordered,
            "simulation_start_to_gpu_end_median_ms": statistics.median(gpu_ms) if gpu_ms else None,
            "note": "Driver interval only; excludes display scanout and physical input latency. "
                    "Missing reports may be capture edges, report lag or aborted frames."}


if __name__ == "__main__":
    trace, log = map(Path, sys.argv[1:])
    result = analyze(trace, log)
    trace.with_suffix(".reflex-driver.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "matched_token_ids"}, indent=2))
