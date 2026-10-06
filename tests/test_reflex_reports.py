import csv
import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "reports", Path(__file__).parents[1] / "tools/streamline_probe/analyze_reflex_reports.py")
reports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reports)


class ReflexReportsTest(unittest.TestCase):
    def run_report(self, line, aborted=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace.csv"
            with trace.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(("event", "related"))
                writer.writerow(("reflex_present_end", 7))
                if aborted:
                    writer.writerow(("reflex_abort", 7))
            # The matching report is in the rotated file, not the active log.
            (root / "ssx_001.1.log").write_text(line)
            (root / "ssx_001.log").write_text(line.replace("token=7", "token=8"))
            return reports.analyze(trace, root / "ssx_001.log")

    def test_matches_exact_token_in_rotated_log(self):
        result = self.run_report("SSX_REFLEX_REPORT token=7 sim=100..200 render=201..300 "
                                 "present=301..400 driver=220..310 queue=221..490 gpu=225..500")
        self.assertEqual(result["matched_token_ids"], [7])
        self.assertEqual(result["missing_driver_token_ids"], [])
        self.assertEqual(result["errors"], [])

    def test_rejects_reversed_timing_and_aborted_token(self):
        line = ("SSX_REFLEX_REPORT token=7 sim=100..200 render=201..300 "
                "present=301..400 driver=220..310 queue=221..490 gpu=225..500")
        for modified, aborted in ((line.replace("201..300", "150..300"), False),
                                  (line, True)):
            result = self.run_report(modified, aborted)
            self.assertEqual(result["matched_ordered_driver_reports"], 0)
            self.assertEqual(len(result["errors"]), 1)


if __name__ == "__main__":
    unittest.main()
