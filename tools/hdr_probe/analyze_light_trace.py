"""Summarize local SSX_HDR_LIGHT records without exporting game images/assets."""
import argparse
import json
import math
import re
from pathlib import Path

HEADER = re.compile(r"SSX_HDR_LIGHT frame=(\d+) post=([0-9A-F]+) reset=(true|false) "
                    r"white=([^ ]+) nonfinite=(\d+)")
ARRAY = re.compile(r"(mean|min|max)=\[([^\]]+)\]")
ROWS = ("exposed", "world_code", "pre_lut", "post_lut", "hdr_nits",
        "sdr_nits", "grade_black", "grade_white")


def read(paths):
    frames = {}
    duplicates = 0
    for path in paths:
        with path.open(encoding="utf-8", errors="replace") as source:
            for line in source:
                match = HEADER.search(line)
                if not match:
                    continue
                arrays = {name: [float(v) for v in values.split(",")]
                          for name, values in ARRAY.findall(line)}
                if set(arrays) != {"mean", "min", "max"} or any(
                        len(values) != 32 for values in arrays.values()):
                    raise ValueError(f"Incomplete trace record: {path}, frame {match[1]}")
                frame = int(match[1])
                duplicates += frame in frames
                frames[frame] = dict(frame=frame, post=match[2], reset=match[3] == "true",
                                     white=float(match[4]), nonfinite=int(match[5]), **arrays)
    return sorted(frames.values(), key=lambda v: v["frame"]), duplicates


def describe(record):
    result = {k: record[k] for k in ("frame", "post", "reset", "white", "nonfinite")}
    for i, name in enumerate(ROWS):
        result[name] = {stat: record[stat][4*i:4*i+4] for stat in ("mean", "min", "max")}
    return result


def summarize(frames, duplicates, count):
    if not frames:
        raise ValueError("No SSX_HDR_LIGHT records found; the diagnostic flag must be enabled.")
    jumps = []
    transitions = []
    for before, after in zip(frames, frames[1:]):
        delta = max(abs(after["mean"][i] - before["mean"][i]) for i in range(16, 19))
        if not math.isfinite(delta):
            delta = 0
        event = dict(mean_hdr_channel_delta_nits=delta, frame_gap=after["frame"]-before["frame"],
                     before=describe(before), after=describe(after))
        jumps.append(event)
        if before["post"] != after["post"]:
            transitions.append(event)
    return dict(frames=len(frames), duplicate_records=duplicates,
                first_frame=frames[0]["frame"], last_frame=frames[-1]["frame"],
                frames_with_nonfinite=sum(v["nonfinite"] != 0 for v in frames),
                frames_without_sdr_reference=sum(v["min"][23] != 1 for v in frames),
                post_shaders=sorted({v["post"] for v in frames}),
                largest_changes=sorted(jumps, key=lambda v: v["mean_hdr_channel_delta_nits"],
                                       reverse=True)[:count],
                post_transitions=transitions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, help="Optional local JSON report")
    parser.add_argument("--largest", type=int, default=12)
    args = parser.parse_args()
    paths = sorted({file for path in args.logs
                    for file in (path.glob("*.log") if path.is_dir() else [path])})
    frames, duplicates = read(paths)
    result = summarize(frames, duplicates, max(args.largest, 0))
    def json_value(value):
        if isinstance(value, float) and not math.isfinite(value):
            return str(value)
        if isinstance(value, dict):
            return {key: json_value(item) for key, item in value.items()}
        if isinstance(value, list):
            return [json_value(item) for item in value]
        return value
    payload = json.dumps(json_value(result), indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(payload + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in result.items()
                          if k not in ("largest_changes", "post_transitions")}, indent=2))
    else:
        print(payload)


if __name__ == "__main__":
    main()
