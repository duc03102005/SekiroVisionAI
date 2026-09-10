"""Regression evidence for native capture trace cohorts and source freshness."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("capture_analyzer", Path(__file__).resolve().parents[1] / "tools/analyze_capture_trace.py")
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)


class CaptureTraceTests(unittest.TestCase):
    def analyze(self, rows, schema=2):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "capture.csv"
            fields = sorted({key for row in rows for key in row})
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fields)
                writer.writeheader()
                writer.writerows(rows)
            meta = dict(schema_version=schema, start_qpc_ms=10000, end_qpc_ms=14000,
                        received=len(rows), trace_full=False, build_revision="test", adapter="fixture",
                        final_state="Stopped", callback_gaps_over_100_ms=0, peak_in_flight=3)
            Path(str(path) + ".meta.json").write_text(json.dumps(meta), encoding="utf-8")
            return analyzer.analyze(path, 0, 4)

    def row(self, sequence, source, dequeue, **extra):
        return dict(sequence=sequence, generation=1, source_qpc_ms=source,
                    dequeue_qpc_ms=dequeue, outcome="source_stale", **extra)

    def test_sixty_dequeues_with_frozen_source_never_claim_fresh_delivery(self):
        rows = [self.row(i + 1, 10000, 10000 + i * 1000 / 60) for i in range(240)]
        result = self.analyze(rows)
        self.assertEqual(result["received_fps"], 60)
        self.assertEqual(result["valid_source_fps"], .25)
        self.assertEqual(result["delivered_fps"], 0)
        self.assertGreater(result["dequeue_age_ms"]["max"], 3000)

    def test_completed_superseded_frame_counts_gpu_copy_but_not_delivery(self):
        rows = [self.row(1, 10001, 10005, ready_observed_qpc_ms=10010),
                self.row(2, 10002, 10006, ready_observed_qpc_ms=10011,
                         consumer_ready_qpc_ms=10013, sink_return_qpc_ms=10023,
                         sink_ms=10, cpu_readback_bytes=4096)]
        rows[0]["outcome"] = "consumer_superseded"
        rows[1]["outcome"] = "copied"
        result = self.analyze(rows)
        self.assertEqual(result["completed"], 2)
        self.assertEqual(result["delivered_to_sink"], 1)
        self.assertEqual(result["gpu_ready_observed_age_ms"]["max"], 9)
        self.assertEqual(result["consumer_ready_age_ms"]["max"], 11)
        self.assertEqual(result["sink_return_age_ms"]["max"], 21)
        self.assertEqual(result["cpu_readback_bytes"], 4096)
        self.assertEqual(result["sink_duration_ms"]["p50"], 10)

    def test_future_source_and_out_of_order_consumer_are_reported(self):
        rows = [self.row(1, 10001, 10005, sink_return_qpc_ms=10030),
                self.row(2, 10002, 10006, sink_return_qpc_ms=10020),
                self.row(3, 90000, 10007)]
        result = self.analyze(rows)
        self.assertEqual(result["valid_source_fps"], .5)
        self.assertEqual(result["delivery_ordering_errors"], 1)

    def test_legacy_trace_does_not_invent_consumer_timing(self):
        rows = [self.row(1, 10001, 10005, ready_observed_qpc_ms=10010)]
        rows[0]["outcome"] = "copied"
        result = self.analyze(rows, schema=1)
        self.assertEqual(result["completed"], 1)
        self.assertIsNone(result["delivered_fps"])
        self.assertIsNone(result["sink_return_age_ms"]["p50"])


if __name__ == "__main__":
    unittest.main()
