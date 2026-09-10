"""SYNTHETIC TEST ONLY: verifies tooling behavior, never Sekiro accuracy/data."""

from __future__ import annotations

from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from DatasetTools.common import load_jsonl, write_json, write_jsonl
from DatasetTools.annotation.contract import new_annotation, validate_annotation
from DatasetTools.annotation.export_samples import export_samples
from DatasetTools.annotation.journal import latest_annotations, save_revision
from DatasetTools.annotation.review_queue import make_review_queue
from DatasetTools.downloader.ingest import ingest, permitted_direct_url, probe_video
from DatasetTools.validation.splits import partition_sources, validate_splits
from DatasetTools.validation.validate import validate_dataset

HAS_CV = importlib.util.find_spec("cv2") is not None
HAS_FFMPEG = bool(shutil.which("ffmpeg") and shutil.which("ffprobe"))


def documented_clip() -> dict:
    return {"clip_id": "SYNTHETIC_ONLY_001", "source_id": "SYNTHETIC_ONLY", "source_group_id": "test-session",
            "start_source_frame": 100, "end_source_frame": 190, "fps_num": 30, "fps_den": 1,
            "boss": "SYNTHETIC SHAPES — NOT A BOSS", "roi": [0.20, 0.12, 0.82, 0.82], "example_only": True}


class ContractAndSplitTests(unittest.TestCase):
    def test_no_hit_cannot_fabricate_contact_and_review_is_explicit(self):
        row = new_annotation(documented_clip(), reviewer="SYNTHETIC TEST ONLY")
        row.update({"windup_start": 115, "active_start": 130, "recovery_start": 160,
                    "impact_evidence": "NO_CONTACT", "impact_frame": 142})
        with self.assertRaisesRegex(ValueError, "Only OBSERVED_CONTACT"):
            validate_annotation(row)
        row["impact_frame"] = None
        validate_annotation(row)
        row["annotation_status"] = "reviewed"
        with self.assertRaisesRegex(ValueError, "reviewer"):
            validate_annotation(row)

    def test_review_journal_revisions_and_synthetic_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "annotations.jsonl"
            clip = documented_clip()
            draft = new_annotation(clip, reviewer="SYNTHETIC TEST ONLY")
            draft.update({"scope": "ENTIRE_CLIP_NON_THREAT", "negative_reason": "CAMERA_ROTATION",
                          "threat_label": False, "impact_evidence": "NO_CONTACT"})
            first = save_revision(path, draft, clip, reviewer="SYNTHETIC TEST ONLY", status="reviewed")
            second = save_revision(path, first, clip, reviewer="SYNTHETIC TEST ONLY", status="rejected")
            self.assertEqual([1, 2], [row["revision"] for row in load_jsonl(path)])
            self.assertEqual("rejected", latest_annotations(path)[0]["annotation_status"])
            with self.assertRaisesRegex(ValueError, "newer annotation"):
                save_revision(path, first, clip, reviewer="SYNTHETIC TEST ONLY", status="reviewed")
            second["example_only"] = False
            with self.assertRaisesRegex(ValueError, "Synthetic"):
                validate_annotation(second, clip)

    def test_duplicate_creator_groups_and_unseen_boss_never_leak(self):
        sources = [
            {"source_id": "a", "creator": "PLAYER_ONE", "boss": "A", "sha256": "duplicate-x"},
            {"source_id": "b", "creator": "player_two", "boss": "B", "sha256": "duplicate-x"},
            {"source_id": "c", "creator": "player_TWO", "boss": "HELD_OUT", "session_id": "same-session"},
            {"source_id": "d", "creator": "player_three", "boss": "A"},
            {"source_id": "e", "creator": "player_four", "boss": "B"},
            {"source_id": "f", "creator": "player_five", "boss": "C"},
        ]
        split = partition_sources(sources, held_out_bosses={"HELD_OUT"})
        self.assertTrue({"a", "b", "c"} <= set(split["heldout_boss_test"]))
        self.assertEqual(1, len(split["train"]))
        self.assertEqual(1, len(split["val"]))
        self.assertEqual(1, len(split["unseen_source_test"]))
        leaked = deepcopy(split)
        leaked["test"].remove("a")
        leaked["train"].append("a")
        with self.assertRaisesRegex(ValueError, "leakage"):
            validate_splits(sources, leaked)

    def test_model_high_score_does_not_become_negative_ground_truth(self):
        clips = [{"clip_id": "a", "source_id": "source", "example_only": True}]
        predictions = [{"clip_id": "a", "model_version": "SYNTHETIC_TEST_ONLY", "threat_probability": 0.97}]
        queue = make_review_queue(predictions, clips)
        self.assertEqual(1, len(queue))
        self.assertEqual("proposed", queue[0]["annotation_status"])
        self.assertIsNone(queue[0]["suggested_label"])
        self.assertTrue(queue[0]["review_required"])

    def test_platform_and_unsafe_url_not_treated_as_direct_media(self):
        for url in ("https://youtube.com/watch?v=abc", "http://example.org/a.mp4",
                    "https://user:secret@example.org/a.mp4", "https://example.org/watch"):
            with self.assertRaises(ValueError):
                permitted_direct_url(url)


@unittest.skipUnless(HAS_CV and HAS_FFMPEG, "OpenCV and FFmpeg required for actual media roundtrip")
class MediaRoundtripTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import cv2
        import numpy as np

        cls.scratch = tempfile.TemporaryDirectory(prefix="svai-SYNTHETIC-TEST-ONLY-")
        cls.root = Path(cls.scratch.name)
        cls.input_video = cls.root / "SYNTHETIC_TEST_ONLY.avi"
        generator = np.random.default_rng(42)
        cls.base = cv2.GaussianBlur(generator.integers(0, 256, (144, 256, 3), dtype=np.uint8), (3, 3), 0)
        writer = cv2.VideoWriter(str(cls.input_video), cv2.VideoWriter_fourcc(*"MJPG"), 30, (256, 144))
        if not writer.isOpened():
            raise RuntimeError("Test fixture MJPEG encoder unavailable.")
        for index in range(210):
            frame = cls.base.copy()
            if 60 <= index < 135:
                x = 65 + (index - 60) % 65
                cv2.rectangle(frame, (x, 40), (x + 48, 105), (235, 35, 60), -1)
            writer.write(frame)
        writer.release()
        cls.data = cls.root / "data"
        cls.source = ingest(input_path=str(cls.input_video), source_id="SYNTHETIC_TEST_ONLY",
                            creator="SYNTHETIC TEST ONLY", session_id="synthetic_session",
                            boss="SYNTHETIC SHAPES — NOT A BOSS", usage_basis="SYNTHETIC_TEST",
                            usage_evidence="Generated colored shapes/textures solely for automated tool tests", root=cls.data)

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def test_global_camera_motion_is_separated_from_local_motion(self):
        import cv2
        import numpy as np
        from DatasetTools.clip_miner.motion import CameraCompensatedMotion

        first = self.base.copy()
        cv2.rectangle(first, (85, 42), (130, 103), (235, 35, 60), -1)
        affine = np.float32([[1, 0, 3], [0, 1, 1]])
        camera = cv2.warpAffine(first, affine, (256, 144), borderMode=cv2.BORDER_REFLECT101)
        local = cv2.warpAffine(self.base, affine, (256, 144), borderMode=cv2.BORDER_REFLECT101)
        cv2.rectangle(local, (105, 43), (150, 104), (235, 35, 60), -1)
        a, b = CameraCompensatedMotion(width=256), CameraCompensatedMotion(width=256)
        a.update(first, 0, 0)
        b.update(first, 0, 0)
        global_signal = a.update(camera, 6, 200)
        local_signal = b.update(local, 6, 200)
        self.assertTrue(global_signal.valid)
        self.assertLess(global_signal.score, 0.15)
        self.assertTrue(local_signal.valid)
        self.assertGreater(local_signal.score, global_signal.score + 0.10)

    def test_ingest_mine_extract_review_and_export_actual_video(self):
        from DatasetTools.clip_miner.mine import mine_source

        output = self.root / "roundtrip"
        clips_manifest = output / "clips.jsonl"
        result = mine_source(self.source, output / "clips", clips_manifest,
                             max_clips=2, threshold=0.18, max_width=256)
        self.assertGreater(result["observations"], 20)
        self.assertGreater(result["clips"], 0)
        clip = load_jsonl(clips_manifest)[0]
        metadata, decoded_pts = probe_video(Path(clip["video_path"]))
        self.assertEqual(clip["frame_count"], len(decoded_pts))
        self.assertEqual("proposed", clip["annotation_status"])
        mapping = load_jsonl(Path(clip["frame_map_path"]))
        self.assertEqual(90, len(mapping))
        self.assertTrue(all(row["source_pts_ms"] <= row["target_source_pts_ms"] + 0.001 for row in mapping))
        annotation = new_annotation(clip, reviewer="SYNTHETIC TEST ONLY")
        annotation.update({"windup_start": mapping[18]["source_frame"], "active_start": mapping[35]["source_frame"],
                           "impact_frame": mapping[43]["source_frame"], "recovery_start": mapping[55]["source_frame"],
                           "recovery_end": mapping[76]["source_frame"], "anchor_frame": mapping[40]["source_frame"],
                           "state": "ACTIVE_ATTACK", "phase": "ACTIVE_ATTACK", "attack_type": "HORIZONTAL_SLASH",
                           "impact_evidence": "OBSERVED_CONTACT", "threat_label": True,
                           "notes": "SYNTHETIC TEST ONLY: boundaries are artificial test assertions, not gameplay labels."})
        journal = output / "annotations.jsonl"
        reviewed = save_revision(journal, annotation, clip, reviewer="SYNTHETIC TEST ONLY", status="reviewed")
        excluded = export_samples(clips_manifest, journal, output / "real-only.jsonl")
        self.assertEqual(0, excluded["samples"])
        report = export_samples(clips_manifest, journal, output / "synthetic-samples.jsonl", allow_synthetic=True)
        self.assertGreater(report["observed_contact_tti_samples"], 0)
        samples = load_jsonl(output / "synthetic-samples.jsonl")
        self.assertTrue(all(row["example_only"] for row in samples))
        self.assertTrue(all(row["labels"]["threat"] is None for row in samples if row["labels"]["state"] == "RECOVERY"))
        first_tti = next(row for row in samples if row["labels"]["tti_ms"] is not None)
        self.assertAlmostEqual(mapping[43]["source_pts_ms"] - first_tti["source_pts_ms"], first_tti["labels"]["tti_ms"], places=4)
        reviewed.update({"impact_evidence": "NO_CONTACT", "impact_frame": None})
        save_revision(journal, reviewed, clip, reviewer="SYNTHETIC TEST ONLY", status="reviewed")
        export_samples(clips_manifest, journal, output / "censored-samples.jsonl", allow_synthetic=True)
        self.assertTrue(all(row["labels"]["tti_ms"] is None for row in load_jsonl(output / "censored-samples.jsonl")))
        checked = validate_dataset(self.data / "source_manifest.jsonl", clips_manifest, journal, verify_hashes=True)
        self.assertEqual(1, checked["reviewed_annotations"])

    def test_variable_frame_rate_source_uses_pts_not_nominal_fps(self):
        from DatasetTools.clip_miner.mine import Candidate, frame_mapping

        video = self.root / "SYNTHETIC_VFR_ONLY.mkv"
        subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-nostdin", "-f", "lavfi",
                        "-i", "testsrc2=size=160x90:rate=30:duration=3",
                        "-vf", "select='not(mod(n,3))+not(mod(n,7))'", "-fps_mode", "vfr",
                        "-c:v", "ffv1", "-threads", "1", str(video)], check=True, capture_output=True)
        metadata, timeline = probe_video(video)
        deltas = {round(b["source_pts_ms"] - a["source_pts_ms"], 1) for a, b in zip(timeline, timeline[1:])}
        self.assertGreater(len(deltas), 1)
        mapping = frame_mapping(timeline, Candidate(0, 2000, 1000, "SYNTHETIC TEST ONLY", 0), 30)
        self.assertTrue(any(row["duplicated"] for row in mapping))
        for row in mapping:
            self.assertEqual(timeline[row["source_frame"]]["source_pts_ms"], row["source_pts_ms"])
            self.assertLessEqual(row["source_pts_ms"], row["target_source_pts_ms"] + 0.001)

    def test_native_recording_import_keeps_exact_qpc_and_review_status(self):
        import cv2
        from DatasetTools.downloader.import_recording import import_recording

        folder = self.root / "native_bundle"
        folder.mkdir()
        frames = []
        pts = 9123456.123456
        for index in range(16):
            name = f"frame-{index:06d}.jpg"
            cv2.imwrite(str(folder / name), self.base)
            frames.append({"index": index, "file": name, "sequence": index * 4 + 1, "generation": 3,
                           "source_qpc_ms": pts, "ready_qpc_ms": pts + 2})
            pts += 71.25 if index % 2 else 94.125
        write_json(folder / "sample.json", {"schema_version": 1, "format": "sekiro-jpeg-frame-bundle",
                   "session_id": "SYNTHETIC_SESSION", "sample_id": "sample-000001", "git_commit": "SYNTHETIC_TEST_ONLY",
                   "marker": {"qpc_ms": frames[7]["source_qpc_ms"], "reason": "FALSE_POSITIVE", "episode": 3},
                   "timing": {"pre_complete": False, "post_complete": False, "completion": "test_truncated"},
                   "frames": frames})
        root = self.root / "native_data"
        imported = import_recording(folder, root, creator="SYNTHETIC TEST ONLY",
                                      usage_evidence="Generated native-bundle fixture, not gameplay", example_only=True)
        timeline = load_jsonl(Path(imported["pts_path"]))
        self.assertEqual([frame["source_qpc_ms"] for frame in frames], [row["source_pts_ms"] for row in timeline])
        self.assertEqual([frame["sequence"] for frame in frames], [row["capture_sequence"] for row in timeline])
        clip = load_jsonl(root / "clips.jsonl")[0]
        self.assertEqual("proposed", clip["annotation_status"])
        self.assertTrue(clip["example_only"])
        checked = validate_dataset(root / "source_manifest.jsonl", root / "clips.jsonl", verify_hashes=True)
        self.assertEqual(1, checked["clips"])


if __name__ == "__main__":
    unittest.main()
