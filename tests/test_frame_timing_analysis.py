import csv
import collections
import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "timing", Path(__file__).parents[1] / "tools/streamline_probe/analyze_frame_timing.py")
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class TimingAnalysisTest(unittest.TestCase):
    def coupled_events(self):
        rows = [
            ("reflex_sleep_begin", 10, 500, 0),
            ("reflex_sleep_end", 10, 500, 0),
            ("reflex_simulation_start", 10, 500, 0),
            ("reflex_input", 10, 500, 0),
            ("worker_input_begin", 11, 10, 0),
            ("sample_write", 12, 10, (1 << 32) | 100),
            ("sample_publish_end", 12, 10, (1 << 32) | 100),
            ("sample_write", 13, 10, (2 << 32) | 200),
            ("sample_publish_end", 13, 10, (2 << 32) | 200),
            ("reflex_simulation_end", 10, 500, 0),
            ("reflex_bind", 10, 500, 99),
            ("sample_select_previous", 99, 12, (1 << 32) | 100),
            ("sample_select_previous", 99, 13, (2 << 32) | 200),
            ("reflex_render_start", 10, 500, 99),
            ("nvidia_token_evaluated", 99, 500, 7),
            ("reflex_render_end", 10, 500, 99),
            ("reflex_present_start", 10, 500, 99),
            ("reflex_present_end", 10, 500, 99),
            ("nvidia_token_presented", 99, 500, 0),
        ]
        events = collections.defaultdict(list)
        for time, (event, rid, related, detail) in enumerate(rows):
            events[event].append(dict(time_ns=time, thread=1, id=rid, related=related,
                                      detail=detail, event=event))
        return events

    def test_coupled_input_and_both_channels(self):
        report = timing.analyze_reflex(self.coupled_events())
        self.assertEqual(report["complete_input_sample_dlaa_present_chains"], 1)
        self.assertEqual(report["identity_or_order_errors"], [])

    def frame_events(self, physics=True):
        events = self.coupled_events()
        for name, group in events.items():
            if name.startswith("reflex_"):
                for row in group:
                    row["id"] = 99
        events["reflex_frame_input"] = [dict(id=99, time_ns=3.1)]
        if physics:
            events["reflex_physics_bind"] = [dict(id=10, related=99, time_ns=3.2)]
            events["reflex_input_consumed"] = [dict(id=10, related=99, detail=u, time_ns=4.1)
                                               for u in range(4)]
        else:
            events["worker_input_begin"] = []
            # Interpolated samples from earlier fixed steps remain valid.
            for name in ("sample_write", "sample_publish_end"):
                for row in events[name]:
                    row["time_ns"] = -1
        return events

    def test_render_driven_allows_interpolated_extra_frames(self):
        for physics in (False, True):
            with self.subTest(physics=physics):
                report = timing.analyze_reflex(self.frame_events(physics))
                self.assertEqual(report["identity_or_order_errors"], [])
                self.assertEqual(report["complete_frame_input_dlaa_present_chains"], 1)
                self.assertEqual(report["physics_jobs_with_verified_input_snapshot"], int(physics))
                self.assertEqual(report["complete_input_sample_dlaa_present_chains"], 0)

    def test_render_driven_rejects_missing_stale_or_early_input(self):
        for fault in ("missing", "stale", "early", "duplicate_user"):
            events = self.frame_events()
            rows = events["reflex_input_consumed"]
            if fault == "missing":
                rows.pop()
            elif fault == "stale":
                rows[0]["related"] = 98
            elif fault == "early":
                rows[0]["time_ns"] = 2
            else:
                rows[0]["detail"] = 1
            with self.subTest(fault=fault):
                report = timing.analyze_reflex(events)
                self.assertEqual(report["complete_frame_input_dlaa_present_chains"], 0)
                self.assertEqual(report["physics_jobs_with_verified_input_snapshot"], 0)
                self.assertIn("physics_input_snapshot_handoff", report["identity_or_order_errors"][0]["reasons"])

    def test_render_driven_rejects_wrong_sample_slot(self):
        events = self.frame_events(False)
        events["sample_select_previous"][0]["detail"] += 1
        report = timing.analyze_reflex(events)
        self.assertEqual(report["complete_frame_input_dlaa_present_chains"], 0)
        self.assertIn("selected_sample_identity", report["identity_or_order_errors"][0]["reasons"])

    def modern_frame_events(self, physics=True):
        events = self.frame_events()
        events["reflex_worker_complete"] = [dict(id=10, related=99, time_ns=8.5,
                                                  detail=1 | (15 << 8) | ((2 | (3 << 16)) if physics else 0))]
        if physics:
            events["reflex_sample_update"] = [dict(id=10, related=99, time_ns=4.5)]
        else:
            events["sample_write"] = []
            events["sample_publish_end"] = []
        return events

    def test_control_only_worker_and_render_without_dlaa(self):
        events = self.modern_frame_events(False)
        events["nvidia_token_evaluated"] = []
        events["sample_select_previous"] = []
        report = timing.analyze_reflex(events)
        self.assertEqual(report["identity_or_order_errors"], [])
        self.assertEqual(report["complete_frame_input_render_present_chains"], 1)
        self.assertEqual(report["complete_frame_input_dlaa_present_chains"], 0)
        self.assertEqual(report["verified_control_only_workers"], 1)
        self.assertEqual(report["worker_updates_with_verified_input_snapshot"], 1)
        self.assertEqual(report["physics_jobs_with_verified_input_snapshot"], 0)

    def test_modern_physics_requires_both_expected_publications(self):
        for missing in (False, True):
            events = self.modern_frame_events()
            if missing:
                events["sample_publish_end"].pop()
            report = timing.analyze_reflex(events)
            self.assertEqual(report["physics_jobs_with_verified_input_snapshot"], 0 if missing else 1)
            self.assertEqual(bool(report["identity_or_order_errors"]), missing)

    def test_frontend_can_select_one_channel_but_dlaa_requires_two(self):
        for dlaa in (False, True):
            events = self.modern_frame_events()
            events["sample_select_previous"].pop()
            if not dlaa:
                events["nvidia_token_evaluated"] = []
            report = timing.analyze_reflex(events)
            self.assertEqual(bool(report["identity_or_order_errors"]), dlaa)
            self.assertEqual(report["complete_frame_input_render_present_chains"], int(not dlaa))

    def test_control_only_marker_cannot_hide_published_physics(self):
        events = self.modern_frame_events()
        events["reflex_worker_complete"][0]["detail"] = 1 | (15 << 8)
        report = timing.analyze_reflex(events)
        self.assertEqual(report["complete_frame_input_render_present_chains"], 0)
        self.assertIn("physics_input_snapshot_handoff", report["identity_or_order_errors"][0]["reasons"])

    def test_coupled_rejects_old_samples_and_wrong_worker_input(self):
        events = self.coupled_events()
        events["sample_write"][0]["related"] = 9
        events["worker_input_begin"][0]["related"] = 9
        events["nvidia_token_evaluated"][0]["related"] = 499
        report = timing.analyze_reflex(events)
        self.assertEqual(report["complete_input_sample_dlaa_present_chains"], 0)
        self.assertEqual(set(report["identity_or_order_errors"][0]["reasons"]),
                         {"selected_sample_ownership", "actual_worker_input", "dlaa_present_token"})

    def test_coupled_rejects_phase_order_and_extra_interpolation(self):
        events = self.coupled_events()
        events["reflex_sleep_end"][0]["time_ns"] = 50
        events["sample_select_next"] = [dict(id=99, related=14)]
        report = timing.analyze_reflex(events)
        self.assertEqual(report["complete_input_sample_dlaa_present_chains"], 0)
        self.assertEqual(set(report["identity_or_order_errors"][0]["reasons"]),
                         {"phase_order", "extra_interpolated_sample"})

    def token_report(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tokens.csv"
            with path.open("w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(("time_ns", "thread", "event", "id", "related", "detail"))
                writer.writerows((i, thread, event, rid, token, detail)
                                 for i, (thread, event, rid, token, detail) in enumerate(rows))
            return timing.analyze(path)["nvidia_token_transport"]

    def test_real_token_chain_repeat_and_capture_edge(self):
        report = self.token_report([
            (1, "nvidia_token_created", 99, 500, 0),
            (1, "gpu_render_marker", 99, 0, 0),
            (1, "reconstruction_begin", 7, 99, 0),
            (1, "nvidia_token_evaluated", 99, 500, 7),
            (1, "reconstruction_end", 7, 0, 1),
            (1, "cp_swap", 42, 7, 99),
            (2, "present_begin", 42, 10, 0),
            (2, "nvidia_token_presented", 99, 500, 0),
            (2, "present_end", 42, 10, 0),
            (2, "present_begin", 42, 11, 0),
            (2, "nvidia_token_repeated", 99, 500, 0),
            (2, "present_end", 42, 11, 0),
            (1, "nvidia_token_created", 100, 501, 0),
            (1, "nvidia_token_dropped", 100, 501, 0),
            (2, "nvidia_token_presented", 98, 499, 0),  # Outside captured image chain.
        ])
        self.assertEqual(report["complete_gpu_marker_dlaa_image_present_chains"], 1)
        self.assertEqual(report["presented_with_incomplete_capture_chain"], 1)
        self.assertEqual(report["identity_or_order_errors"], [])
        self.assertEqual(report["events"]["repeated"], 1)
        self.assertEqual(report["events"]["dropped"], 1)

    def test_rejects_wrong_token_wrong_image_and_terminal_reuse(self):
        report = self.token_report([
            (1, "nvidia_token_created", 99, 500, 0),
            (1, "reconstruction_begin", 7, 98, 0),
            (1, "nvidia_token_evaluated", 99, 501, 7),
            (1, "cp_swap", 42, 8, 100),
            (1, "nvidia_token_dropped", 99, 501, 0),
            (2, "present_begin", 42, 10, 0),
            (2, "nvidia_token_presented", 99, 501, 0),
            (2, "present_end", 42, 10, 1),
            (2, "nvidia_token_presented", 99, 501, 0),
        ])
        self.assertEqual(report["complete_gpu_marker_dlaa_image_present_chains"], 0)
        reasons = {reason for error in report["identity_or_order_errors"] for reason in error["reasons"]}
        self.assertTrue({"token_identity_changed", "reconstruction_identity",
                         "dropped_and_presented", "duplicate_presented"} <= reasons)

    def test_rejects_image_mismatch_and_hresult_mismatch(self):
        report = self.token_report([
            (1, "nvidia_token_created", 99, 500, 0),
            (1, "reconstruction_begin", 7, 99, 0),
            (1, "nvidia_token_evaluated", 99, 500, 7),
            (1, "cp_swap", 42, 8, 100),
            (2, "present_begin", 42, 10, 0),
            (2, "nvidia_token_presented", 99, 500, 0),
            (2, "present_end", 42, 10, 1),
        ])
        self.assertEqual(set(report["identity_or_order_errors"][0]["reasons"]),
                         {"present_image_identity", "present_interval_or_result"})

    def test_sample_generations_and_ordered_gpu_chain(self):
        rows = [
            (1, 1, "sample_write", 10, 101, 500),
            (2, 1, "sample_write", 11, 102, 501),
            (3, 2, "sample_select_previous", 99, 10, 500),
            (4, 2, "sample_select_next", 99, 11, 501),
            (5, 3, "gpu_render_marker", 99, 0, 0),
            (6, 3, "reconstruction_begin", 7, 99, 0),
            (7, 2, "guest_swap", 42, 99, 0),
            (8, 3, "cp_swap", 42, 7, 99),
            (9, 2, "sample_select_previous", 100, 0, 600),
            (10, 2, "sample_select_previous", 100, 12, 602),
            (11, 2, "sample_select_previous", 100, 11, 999),
            (12, 2, "guest_swap", 43, 100, 0),
            (13, 3, "cp_swap", 43, 8, 99),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.csv"
            with path.open("w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(("time_ns", "thread", "event", "id", "related", "detail"))
                writer.writerows(rows)
            report = timing.analyze(path)
        self.assertEqual(report["ordered_gpu_marker_to_reconstruction_to_swap_chains"], 1)
        self.assertEqual(report["gpu_marker_swap_mismatch_ids"], [43])
        lineage = report["sample_lineage"]
        self.assertEqual(lineage["linked_to_captured_write"], 2)
        self.assertEqual(lineage["unknown_generation"], 1)
        self.assertEqual(lineage["generation_outside_capture"], 1)
        self.assertEqual(lineage["slot_mismatches"], 1)
        self.assertEqual(lineage["renders_with_multiple_worker_generations"], 1)
        self.assertFalse(report["reflex_markers_emitted"])

    def test_repeated_presents_keep_image_identity_and_early_reconstruction(self):
        # Reconstruction is keyed by CP frame 7; only its later swap explicitly
        # associates that frame with CPU swap 42. Two paints show the same image.
        rows = [
            (1, 1, "reconstruction_begin", 7, 0, 0),
            (2, 1, "reconstruction_end", 7, 0, 1),
            (3, 2, "guest_swap", 42, 99, 0),
            (4, 1, "cp_swap", 42, 7, 0),
            (5, 3, "present_begin", 42, 100, 0),
            (6, 3, "present_end", 42, 100, 0),
            (7, 3, "present_begin", 42, 101, 0),
            (8, 3, "present_end", 42, 101, 0),
            # Censored at the end of the capture, not counted as successful.
            (9, 3, "present_begin", 43, 102, 0),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.csv"
            with path.open("w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(("time_ns", "thread", "event", "id", "related", "detail"))
                writer.writerows(rows)
            report = timing.analyze(path)
        self.assertEqual(report["successful_present_calls"], 2)
        self.assertEqual(report["distinct_presented_swap_ids"], 1)
        self.assertEqual(report["repeated_present_calls"], 1)
        self.assertEqual(report["complete_guest_to_cp_to_present_ids"], 1)
        self.assertEqual(report["reconstruction_started_before_cpu_swap"], 1)
        self.assertFalse(report["simulation_ownership_verified"])
        self.assertFalse(report["reflex_markers_emitted"])


if __name__ == "__main__":
    unittest.main()
