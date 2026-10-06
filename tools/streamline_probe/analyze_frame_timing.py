"""Analyze native SSX timing identities. No inference of simulation ownership.

python tools/streamline_probe/analyze_frame_timing.py out/reflex-timing/timing-*.csv
Writes a neighboring .json report. Requires only Python's standard library.
"""
import collections
import csv
import json
import statistics
import struct
import sys
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {"count": len(values), "median_ms": statistics.median(values),
            "p95_ms": values[int((len(values) - 1) * .95)], "max_ms": max(values)}


def analyze_tokens(rows, events, by_event_id, cp):
    # Render ID and NVIDIA token ID must both match. Never infer association
    # from whichever token was allocated most recently on another thread.
    names = ("created", "evaluated", "presented", "dropped", "repeated")
    tokens = collections.defaultdict(lambda: collections.defaultdict(list))
    ids_per_render = collections.defaultdict(set)
    active_paints = {}
    token_paints = {}
    for row in rows:
        event = row["event"]
        if event == "present_begin":
            active_paints[row["thread"]] = row
        elif event == "present_end":
            active_paints.pop(row["thread"], None)
        elif event.startswith("nvidia_token_"):
            kind = event.removeprefix("nvidia_token_")
            if kind not in names:
                continue
            key = (row["id"], row["related"])
            tokens[key][kind].append(row)
            ids_per_render[row["id"]].add(row["related"])
            if kind == "presented":
                token_paints[key] = active_paints.get(row["thread"])
    complete_chains = 0
    incomplete = 0
    errors = []
    ends = {(r["id"], r["related"]): r for r in events["present_end"]}
    for key, stages in tokens.items():
        render, token = key
        reasons = []
        for kind in names[:-1]:
            if len(stages[kind]) > 1:
                reasons.append("duplicate_" + kind)
        if stages["dropped"] and stages["presented"]:
            reasons.append("dropped_and_presented")
        if len(ids_per_render[render]) != 1:
            reasons.append("token_identity_changed")
        ordered = [stages[k][0] for k in ("created", "evaluated", "presented") if stages[k]]
        if any(a["time_ns"] > b["time_ns"] for a, b in zip(ordered, ordered[1:])):
            reasons.append("token_stage_order")
        if stages["evaluated"]:
            evaluation = stages["evaluated"][0]
            reconstruction = by_event_id.get(("reconstruction_begin", evaluation["detail"]))
            if reconstruction and reconstruction["related"] != render:
                reasons.append("reconstruction_identity")
        if stages["presented"]:
            presented = stages["presented"][0]
            paint = token_paints.get(key)
            swap = cp.get(paint["id"]) if paint else None
            end = ends.get((paint["id"], paint["related"])) if paint else None
            if swap and (swap["detail"] != render or
                         (stages["evaluated"] and swap["related"] != evaluation["detail"])):
                reasons.append("present_image_identity")
            if end and (end["detail"] != presented["detail"] or
                        not paint["time_ns"] <= presented["time_ns"] <= end["time_ns"]):
                reasons.append("present_interval_or_result")
            marker = by_event_id.get(("gpu_render_marker", render))
            reconstructed = (by_event_id.get(("reconstruction_end", evaluation["detail"]))
                             if stages["evaluated"] else None)
            full = (len(ordered) == 3 and marker and swap and end and reconstructed
                    and reconstruction)
            if full and not reasons and presented["detail"] == 0 and reconstructed["detail"] == 1:
                # The CP allocates the token in OnGuestRenderMarker, then logs
                # successful parsing of that same packet before any draws.
                if (ordered[0]["time_ns"] <= marker["time_ns"] < reconstruction["time_ns"]
                        <= evaluation["time_ns"] <= reconstructed["time_ns"]
                        <= swap["time_ns"] <= paint["time_ns"]):
                    complete_chains += 1
                else:
                    reasons.append("render_to_present_order")
            elif not full:
                incomplete += 1
        if reasons:
            errors.append({"render_id": render, "token_id": token, "reasons": reasons})
    return {
        "events": {name: len(events["nvidia_token_" + name]) for name in names},
        "complete_gpu_marker_dlaa_image_present_chains": complete_chains,
        "presented_with_incomplete_capture_chain": incomplete,
        "identity_or_order_errors": errors,
        "note": "Token transport only. Repeated paints are not new frames. "
                "No NVIDIA PCL markers, Reflex pacing or scanout measurements are inferred.",
    }


def analyze_reflex(events):
    """Prove input/sample ownership independently of temporal render observations."""
    order = ("sleep_begin", "sleep_end", "simulation_start", "input",
             "simulation_end", "bind", "render_start", "render_end",
             "present_start", "present_end")
    frames = collections.defaultdict(lambda: collections.defaultdict(list))
    for kind in (*order, "abort"):
        for row in events["reflex_" + kind]:
            frames[(row["id"], row["related"])][kind].append(row)
    writes = {r["id"]: r for r in events["sample_write"]}
    published = {r["id"]: r for r in events["sample_publish_end"]}
    complete = incomplete = aborted = frame_complete = physics_handoffs = 0
    render_complete = control_workers = snapshot_workers = 0
    modern_workers = bool(events["reflex_worker_complete"] or events["reflex_sample_update"])
    errors, worker_intervals, frame_intervals = [], [], []
    for (worker, token), stages in frames.items():
        if stages["abort"]:
            aborted += 1
            if stages["present_end"]:
                errors.append({"worker": worker, "token": token, "reasons": ["aborted_and_presented"]})
            continue
        if not all(stages[k] for k in order):
            incomplete += 1
            continue
        reasons = []
        valid_handoffs = valid_controls = valid_snapshots = 0
        if any(len(stages[k]) != 1 for k in order):
            reasons.append("duplicate_phase")
        chain = [stages[k][0] for k in order]
        if any(a["time_ns"] > b["time_ns"] for a, b in zip(chain, chain[1:])):
            reasons.append("phase_order")
        render = stages["bind"][0]["detail"]
        if not render or any(stages[k][0]["detail"] != render for k in order[6:]):
            reasons.append("render_identity")
        start, end = stages["simulation_start"][0], stages["simulation_end"][0]
        frame_inputs = [r for r in events["reflex_frame_input"] if r["id"] == worker]
        if frame_inputs:
            # Each render performs a real host-input update. Physics is serviced
            # only when its original fixed-rate dispatcher has a due job. Other
            # rendered frames intentionally retain interpolation of older states.
            if len(frame_inputs) != 1 or not start["time_ns"] <= frame_inputs[0]["time_ns"] <= end["time_ns"]:
                reasons.append("frame_input_interval")
            bound = [r for r in events["reflex_physics_bind"] if r["related"] == worker]
            if len(bound) > 1:
                reasons.append("multiple_physics_jobs")
            for binding in bound:
                physics = binding["id"]
                actual = [r for r in events["worker_input_begin"] if r["related"] == physics]
                consumed = [r for r in events["reflex_input_consumed"] if r["id"] == physics]
                pubs = [r for r in events["sample_publish_end"] if r["related"] == physics]
                updates = [r for r in events["reflex_sample_update"] if r["id"] == physics]
                finished = [r for r in events["reflex_worker_complete"] if r["id"] == physics]
                valid = frame_inputs[0]["time_ns"] <= binding["time_ns"] <= end["time_ns"]
                expects_input, expects_samples = True, True  # Earlier trace format.
                finish_time = end["time_ns"]
                if modern_workers:
                    valid &= len(finished) == 1
                    if finished:
                        finish = finished[0]
                        expects_input, expects_samples = bool(finish["detail"] & 1), bool(finish["detail"] & 2)
                        finish_time = finish["time_ns"]
                        valid &= (finish["related"] == worker and binding["time_ns"] <= finish_time <= end["time_ns"]
                                  and ((finish["detail"] >> 8) & 15) == (15 if expects_input else 0)
                                  and ((finish["detail"] >> 16) & 3) == (3 if expects_samples else 0))
                    valid &= len(updates) == int(expects_samples)
                    valid &= all(r["related"] == worker and binding["time_ns"] <= r["time_ns"] <= finish_time
                                 for r in updates)
                valid &= len(actual) == int(expects_input) and len(consumed) == (4 if expects_input else 0)
                valid &= {r["detail"] for r in consumed} == (set(range(4)) if expects_input else set())
                input_time = actual[0]["time_ns"] if actual else binding["time_ns"]
                valid &= binding["time_ns"] <= input_time <= finish_time
                valid &= all(r["related"] == worker and input_time <= r["time_ns"] <= finish_time for r in consumed)
                valid &= len(pubs) == (2 if expects_samples else 0)
                valid &= len({r["detail"] >> 32 for r in pubs}) == (2 if expects_samples else 0)
                publication_start = updates[0]["time_ns"] if updates else input_time
                valid &= all(publication_start <= r["time_ns"] <= finish_time for r in pubs)
                if not valid:
                    reasons.append("physics_input_snapshot_handoff")
                else:
                    valid_handoffs += int(expects_input and expects_samples)
                    valid_controls += int(not expects_samples)
                    valid_snapshots += int(expects_input)
            inputs = frame_inputs
        else:
            inputs = [r for r in events["worker_input_begin"] if r["related"] == worker]
        if len(inputs) != 1 or not (start["time_ns"] <= stages["input"][0]["time_ns"]
                                    <= inputs[0]["time_ns"] <= end["time_ns"]):
            reasons.append("actual_worker_input")
        evaluated = [r for r in events["nvidia_token_evaluated"] if r["id"] == render]
        selections = [r for r in events["sample_select_previous"] if r["id"] == render]
        needs_samples = not frame_inputs or bool(evaluated)
        # Ordinary frontend rendering may select one channel or none. DLAA and
        # the original coupled mode require the complete two-channel scene.
        channels = {r["detail"] >> 32 for r in selections}
        if ((needs_samples and len(selections) != 2) or len(selections) > 2 or
                len(channels) != len(selections)):
            reasons.append("two_sample_channels")
        for selected in selections:
            write = writes.get(selected["related"])
            publish = published.get(selected["related"])
            if frame_inputs:
                if not selected["related"] and needs_samples:
                    reasons.append("unknown_selected_generation")
                if write and (write["detail"] != selected["detail"] or
                              write["time_ns"] > selected["time_ns"]):
                    reasons.append("selected_sample_identity")
                if selected["time_ns"] < end["time_ns"]:
                    reasons.append("sample_selection_before_update_end")
                continue
            if (not write or not publish or write["related"] != worker or
                    publish["related"] != worker or
                    not write["detail"] == publish["detail"] == selected["detail"] or
                    not start["time_ns"] <= write["time_ns"] <= publish["time_ns"]
                    <= end["time_ns"] <= selected["time_ns"]):
                reasons.append("selected_sample_ownership")
        if not frame_inputs and any(r["id"] == render and r["related"] for r in events["sample_select_next"]):
            reasons.append("extra_interpolated_sample")
        presented = [r for r in events["nvidia_token_presented"]
                     if r["id"] == render and r["related"] == token]
        if (len(presented) != 1 or presented[0]["detail"] != 0 or
                (not frame_inputs and len(evaluated) != 1) or len(evaluated) > 1 or
                any(r["related"] != token or not stages["render_start"][0]["time_ns"] <= r["time_ns"]
                    <= stages["render_end"][0]["time_ns"] for r in evaluated)):
            reasons.append("dlaa_present_token")
        if reasons:
            errors.append({"worker": worker, "token": token, "reasons": sorted(set(reasons))})
        else:
            if frame_inputs:
                render_complete += 1
                frame_complete += int(bool(evaluated))
                physics_handoffs += valid_handoffs
                control_workers += valid_controls
                snapshot_workers += valid_snapshots
                frame_intervals.append((stages["present_end"][0]["time_ns"] - inputs[0]["time_ns"]) / 1e6)
            else:
                complete += 1
                worker_intervals.append((stages["present_end"][0]["time_ns"] - inputs[0]["time_ns"]) / 1e6)
    bound_workers = {r["id"] for r in events["reflex_physics_bind"]}
    captured_workers = ({r["id"] for r in events["worker_begin"]} &
                        {r["id"] for r in events["worker_end"]})
    unbound = [r["related"] for r in events["worker_input_begin"]
               if r["related"] in captured_workers and r["related"] not in bound_workers]
    return {"mode": "render_driven" if events["reflex_frame_input"] else "coupled_or_absent",
            "complete_input_sample_dlaa_present_chains": complete,
            "complete_frame_input_dlaa_present_chains": frame_complete,
            "complete_frame_input_render_present_chains": render_complete,
            "complete_frames_without_dlaa": render_complete - frame_complete,
            "verified_control_only_workers": control_workers,
            "worker_updates_with_verified_input_snapshot": snapshot_workers,
            "physics_jobs_with_verified_input_snapshot": physics_handoffs,
            "unbound_captured_physics_input_calls": len(unbound) if events["reflex_frame_input"] else 0,
            "incomplete_capture_chains": incomplete, "aborted_frames": aborted,
            "identity_or_order_errors": errors,
            "worker_input_to_present_return_ms": distribution(worker_intervals),
            "host_frame_input_to_present_return_ms": distribution(frame_intervals),
            "note": "Coupled mode proves exact selected worker ownership. Render-driven mode proves "
                    "per-frame input acquisition and due-worker snapshot consumption; interpolation "
                    "still uses earlier physics states. Neither measures physical input-to-photon latency."}


def analyze(path):
    with path.open(newline="") as f:
        rows = [{k: v if k == "event" else int(v) for k, v in row.items()}
                for row in csv.DictReader(f)]
    events = collections.defaultdict(list)
    by_event_id = {}
    for r in rows:
        events[r["event"]].append(r)
        by_event_id[(r["event"], r["id"])] = r
    if any(a["time_ns"] > b["time_ns"] for a, b in zip(rows, rows[1:])):
        raise ValueError("Trace records are not monotonic")
    durations = {}
    for kind in ("input", "worker", "worker_dispatch", "render", "render_dispatch",
                 "synchronized_render", "reconstruction"):
        durations[kind] = distribution([
            (end["time_ns"] - begin["time_ns"]) / 1e6
            for begin in events[kind + "_begin"]
            if (end := by_event_id.get((kind + "_end", begin["id"])))
        ])
    swaps = {r["id"]: r for r in events["guest_swap"] if r["id"]}
    cp = {r["id"]: r for r in events["cp_swap"] if r["id"]}
    # Present pairs use BOTH image ID and paint submission: UI repainting can
    # present the same image more than once. Trace edges may be incomplete.
    starts = {(r["id"], r["related"]): r for r in events["present_begin"]}
    complete = [r for r in events["present_end"]
                if (r["id"], r["related"]) in starts and r["detail"] == 0]
    seen = collections.Counter(r["id"] for r in complete if r["id"])
    durations["guest_swap_to_cp"] = distribution([
        (cp[i]["time_ns"] - r["time_ns"]) / 1e6 for i, r in swaps.items() if i in cp])
    durations["guest_swap_to_present_return"] = distribution([
        (r["time_ns"] - swaps[r["id"]]["time_ns"]) / 1e6
        for r in complete if r["id"] in swaps])
    durations["present_call"] = distribution([
        (r["time_ns"] - starts[(r["id"], r["related"])]["time_ns"]) / 1e6
        for r in complete])
    reconstruction_before_guest_swap = 0
    mapped_reconstruction = 0
    for i, s in swaps.items():
        if i in cp and (recon := by_event_id.get(("reconstruction_begin", cp[i]["related"]))):
            mapped_reconstruction += 1
            reconstruction_before_guest_swap += recon["time_ns"] < s["time_ns"]
    writes = {r["id"]: r for r in events["sample_write"] if r["id"]}
    selections = events["sample_select_previous"] + events["sample_select_next"]
    linked_selections = [r for r in selections if r["related"] in writes
                         and writes[r["related"]]["detail"] == r["detail"]]
    workers_per_render = collections.defaultdict(set)
    for r in linked_selections:
        if worker := writes[r["related"]]["related"]:
            workers_per_render[r["id"]].add(worker)
    gpu_markers = {r["id"]: r for r in events["gpu_render_marker"]}
    first_gpu_marker_ns = min((r["time_ns"] for r in gpu_markers.values()), default=None)
    unmarked_recon = [r for r in events["reconstruction_begin"] if not r["related"]]
    unmarked_prefix = sum(first_gpu_marker_ns is not None and r["time_ns"] < first_gpu_marker_ns
                          for r in unmarked_recon)
    selections_by_render = collections.defaultdict(lambda: collections.defaultdict(dict))
    for r in linked_selections:
        selections_by_render[r["id"]][r["detail"] >> 32][r["event"]] = writes[r["related"]]["related"]
    complete_channels = matching_pairs = 0
    for channels in selections_by_render.values():
        pairs = [(s["sample_select_previous"], s["sample_select_next"])
                 for s in channels.values()
                 if s.get("sample_select_previous") and s.get("sample_select_next")]
        if len(channels) == len(pairs) == 2:
            complete_channels += 1
            matching_pairs += pairs[0] == pairs[1]
    worker_ids = {r["id"] for r in events["worker_begin"]}
    ordered_chains = 0
    mismatches = []
    for i, s in swaps.items():
        if i not in cp:
            continue
        c = cp[i]
        recon = by_event_id.get(("reconstruction_begin", c["related"]))
        # Zero IDs reflect absent instrumentation, alternate paths or capture
        # edges. Count separately, never claim a proven token association.
        if c["detail"] and c["detail"] != s["related"]:
            mismatches.append(i)
        if (recon and c["detail"] and c["detail"] == s["related"] == recon["related"]
                and (marker := gpu_markers.get(c["detail"]))
                and marker["time_ns"] < recon["time_ns"]):
            ordered_chains += 1
    factors = [struct.unpack("!f", r["related"].to_bytes(4, "big"))[0]
               for r in events["sample_interpolation"]]
    reflex = analyze_reflex(events)
    capture_seconds = (rows[-1]["time_ns"] - rows[0]["time_ns"]) / 1e9 if len(rows) > 1 else 0
    return {
        "capture_seconds": capture_seconds,
        "distinct_presented_frames_per_second": len(seen) / capture_seconds if capture_seconds else None,
        "records": len(rows), "event_counts": {k: len(v) for k, v in events.items()},
        "threads": {k: sorted({r["thread"] for r in v}) for k, v in events.items()},
        "successful_present_calls": len(complete), "distinct_presented_swap_ids": len(seen),
        "repeated_present_calls": sum(v - 1 for v in seen.values()),
        "complete_guest_to_cp_to_present_ids": len(swaps.keys() & cp.keys() & seen.keys()),
        "swaps_without_render_scope": sum(not r["related"] for r in swaps.values()),
        "mapped_reconstruction_frames": mapped_reconstruction,
        "reconstruction_started_before_cpu_swap": reconstruction_before_guest_swap,
        "ordered_gpu_marker_to_reconstruction_to_swap_chains": ordered_chains,
        "gpu_marker_swap_mismatch_ids": mismatches,
        "reconstructions_without_ordered_marker": len(unmarked_recon),
        "unmarked_reconstructions_before_first_gpu_marker": unmarked_prefix,
        "unmarked_reconstructions_after_first_gpu_marker": len(unmarked_recon) - unmarked_prefix,
        "sample_lineage": {
            "selections": len(selections), "linked_to_captured_write": len(linked_selections),
            "unknown_generation": sum(not r["related"] for r in selections),
            "generation_outside_capture": sum(bool(r["related"]) and r["related"] not in writes
                                               for r in selections),
            "slot_mismatches": sum(r["related"] in writes and
                                   writes[r["related"]]["detail"] != r["detail"]
                                   for r in selections),
            "renders_with_multiple_worker_generations": sum(len(w) > 1 for w in workers_per_render.values()),
            "complete_two_channel_render_selections": complete_channels,
            "matching_worker_pairs_across_both_channels": matching_pairs,
            "worker_input_calls_linked_to_captured_worker": sum(
                r["related"] in worker_ids for r in events["worker_input_begin"]),
            "selection_count_histogram": dict(collections.Counter(
                str(r["detail"] & 0xffffffff) for r in events["sample_interpolation"])),
            "interpolation_factor_min": min(factors) if factors else None,
            "interpolation_factor_max": max(factors) if factors else None,
        },
        "nvidia_token_transport": analyze_tokens(rows, events, by_event_id, cp),
        "duration_ms": durations,
        "coupled_reflex": reflex,
        "frame_input_handoff_verified": bool(reflex["complete_frame_input_dlaa_present_chains"]
                                             and reflex["physics_jobs_with_verified_input_snapshot"]
                                             and not reflex["identity_or_order_errors"]
                                             and not reflex["unbound_captured_physics_input_calls"]
                                             and not reflex["aborted_frames"]),
        "simulation_ownership_verified": bool(reflex["complete_input_sample_dlaa_present_chains"]
                                              and not reflex["identity_or_order_errors"]),
        "reflex_markers_emitted": bool(events["reflex_simulation_start"]),
        "note": "Worker IDs on render events are temporal observations, not proof of consumed state. "
                "CPU timestamps include diagnostic overhead; Present return is not scanout. "
                "Missing events at capture edges are not necessarily dropped frames.",
    }


if __name__ == "__main__":
    for name in sys.argv[1:]:
        path = Path(name)
        report = analyze(path)
        path.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"file": str(path), **report}, indent=2))
