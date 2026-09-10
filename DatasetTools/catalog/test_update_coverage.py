"""Isolated manifest fixtures; never represent these bytes as gameplay data."""

from copy import deepcopy
import csv
import json
from pathlib import Path
import tempfile
import unittest

from DatasetTools.annotation.contract import new_annotation
from DatasetTools.catalog.update_coverage import build_coverage, update_coverage
from DatasetTools.common import write_jsonl


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.paths = {key: self.root / name for key, name in (
            ("catalog", "catalog.csv"), ("sources", "sources.jsonl"),
            ("clips", "clips.jsonl"), ("annotations", "annotations.jsonl"),
            ("taxonomy", "taxonomy.json"), ("existing", "coverage.csv"))}
        self.paths["taxonomy"].write_text(json.dumps({"bosses": [
            {"name": "Boss A", "aliases": ["Alias A"], "phase_keys": ["UNREVIEWED"]},
            {"name": "Boss B", "phase_keys": ["UNREVIEWED"]}]}))

    def source(self, identifier="s1", synthetic=False):
        media = self.root / (identifier + ".fixture")
        media.write_bytes(b"unit-test fixture; not gameplay")
        return {"source_id": identifier, "boss": "Boss A", "source_group_id": "session1",
                "player_id": "Player1", "creator": "Player1", "sha256": "a" * 64,
                "media_path": str(media), "download_status": "IMPORTED",
                "usage_basis": "SYNTHETIC_TEST" if synthetic else "OWN_RECORDING",
                "usage_evidence": "Unit-test manifest fixture, never used for real coverage",
                "example_only": synthetic}

    def clip(self, identifier, source, start_ms, end_ms):
        media = self.root / (identifier + ".fixture")
        media.write_bytes(b"unit-test clip; not gameplay")
        return {"clip_id": identifier, "source_id": source["source_id"],
                "source_group_id": source["source_group_id"], "boss": "Boss A",
                "source_sha256": source["sha256"], "video_path": str(media),
                "start_source_frame": int(start_ms / 10), "end_source_frame": int(end_ms / 10),
                "source_start_ms": start_ms, "source_end_ms": end_ms,
                "fps_num": 100, "fps_den": 1, "example_only": source["example_only"]}

    def annotation(self, identifier, clip, *, negative=False, danger=True):
        row = new_annotation(clip)
        row.update(annotation_id=identifier, event_id=identifier, strike_id=identifier,
                   annotation_status="reviewed", revision=1, reviewer="fixture-reviewer",
                   reviewed_at="2026-09-10T00:00:00Z", boss="Alias A")
        if negative:
            row.update(scope="ENTIRE_CLIP_NON_THREAT", negative_reason="CAMERA_ROTATION",
                       threat_label=False)
        else:
            begin = clip["start_source_frame"]
            row.update(state="ATTACK_WINDUP", phase="ATTACK_WINDUP", windup_start=begin,
                       active_start=begin + 1, attack_type="HORIZONTAL_SLASH", threat_label=danger)
        return row

    def test_missing_inputs_and_catalog_links_do_not_create_coverage(self):
        self.paths["catalog"].write_text("source_id,boss,url\nREFERENCE,Alias A,https://example.com/video\n")
        self.paths["existing"].write_text("boss,phase\nRetained Boss,Phase 2\n")
        rows, summary = build_coverage(**self.paths)
        self.assertEqual(len(rows), 3)
        self.assertTrue(all(row["status"] == "EMPTY" and row["number_of_sources"] == 0 for row in rows))
        self.assertIn("1 boss-specific catalog references", rows[0]["notes"])
        self.assertEqual(len(summary["missing_inputs"]), 3)

    def test_union_counts_deduplication_and_synthetic_exclusion(self):
        source = self.source()
        duplicate = self.source("s2")
        synthetic = self.source("synthetic", synthetic=True)
        clips = [self.clip("attack", source, 0, 3000), self.clip("negative", source, 2000, 5000),
                 self.clip("duplicate", duplicate, 0, 3000), self.clip("miss", source, 8000, 9000),
                 self.clip("synthetic_clip", synthetic, 0, 3000)]
        labels = [self.annotation("a1", clips[0]), self.annotation("a2", clips[0]),
                  self.annotation("n1", clips[1], negative=True), self.annotation("d1", clips[2]),
                  self.annotation("m1", clips[3], danger=False), self.annotation("s1", clips[4])]
        labels[0]["camera_conditions"] = ["fixed-close", "UNKNOWN"]
        write_jsonl(self.paths["sources"], [source, duplicate, synthetic])
        write_jsonl(self.paths["clips"], clips)
        write_jsonl(self.paths["annotations"], labels)
        rows, summary = build_coverage(**self.paths)
        row = rows[0]
        self.assertEqual(row["number_of_sources"], 1)
        self.assertEqual(row["attack_clips"], 2)
        self.assertEqual(row["positive_attack_clips"], 1)
        self.assertEqual(row["negative_clips"], 1)
        self.assertEqual(row["different_players"], 1)
        self.assertEqual(row["different_camera_conditions"], 1)
        self.assertAlmostEqual(row["total_minutes"], 0.1, places=6)
        self.assertEqual(summary["excluded"]["synthetic_sources"], 1)
        self.assertEqual(summary["duplicate_collapsed_sources"], 1)

    def test_latest_rejection_and_failed_update_preserve_output(self):
        source = self.source()
        clip = self.clip("clip", source, 0, 3000)
        positive = self.annotation("a1", clip)
        rejected = deepcopy(positive)
        rejected.update(revision=2, annotation_status="rejected")
        write_jsonl(self.paths["sources"], [source])
        write_jsonl(self.paths["clips"], [clip])
        write_jsonl(self.paths["annotations"], [positive, rejected])
        rows, _ = build_coverage(**self.paths)
        self.assertEqual(rows[0]["status"], "EMPTY")
        destination = self.root / "result.csv"
        destination.write_text("existing report\n")
        write_jsonl(self.paths["annotations"], [positive, self.annotation("n1", clip, negative=True)])
        with self.assertRaisesRegex(ValueError, "conflicting"):
            update_coverage(output=destination, **self.paths)
        self.assertEqual(destination.read_text(), "existing report\n")


if __name__ == "__main__":
    unittest.main()
