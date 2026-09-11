"""Behavioral checks for leakage, causality, missing labels and review-only mining."""
import json
from pathlib import Path
import tempfile
import unittest

import cv2
import numpy as np
import torch

from Training.datasets.splits import make_splits, validate_splits
from Training.datasets.video_samples import target_values, temporal_indices, preprocess_rgb, VideoSamples
from Training.models import build_model
from Training.models.temporal import ARCHITECTURES, CausalBlock, CausalAttention, CausalLucasKanade
from Training.losses.multitask import multitask_loss
from Training.trainers.smoke import synthetic_batch
from Training.trainers.pretrain import contrastive_loss
from Training.datasets.reviewed_intervals import export as export_intervals
from Training.metrics.quality import summarize_predictions
from Evaluation.false_positive.mine import review_queue
from Evaluation.offline.events import evaluate_events


class TrainingContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_transitive_source_player_duplicate_leakage_rejected(self):
        rows = [{"source_id": "a", "player_id": "p"},
                {"source_id": "b", "player_id": "p", "duplicate_group_id": "d"},
                {"source_id": "c", "duplicate_group_id": "d"}]
        with self.assertRaises(ValueError):
            validate_splits(rows, {"train": {"a"}, "test": {"c"}})
        split = make_splits(rows)
        self.assertEqual(split["train"], {"a", "b", "c"})
        self.assertFalse(split["test"])

    def test_heldout_boss_moves_entire_shared_source_group(self):
        rows = [{"source_id": "a", "boss": "held", "player_id": "p"},
                {"source_id": "b", "boss": "known", "player_id": "p"},
                {"source_id": "c", "boss": "known", "player_id": "q"}]
        split = make_splits(rows, holdout_bosses=["held"])
        self.assertEqual(split["heldout_boss"], {"a", "b"})
        self.assertEqual(split["train"], {"c"})

    def test_no_hit_never_becomes_point_tti_or_negative_threat(self):
        values, masks = target_values({"labels": {"attack": True, "threat": None,
                                  "tti_ms": 120, "tti_censored": True, "impact_evidence": "NO_CONTACT"}})
        self.assertEqual(float(masks["tti"]), 0)
        self.assertEqual(float(masks["threat"]), 0)
        self.assertEqual(float(masks["attack"]), 1)
        self.assertEqual(float(values["tti"]), 0)

    def test_insufficient_history_rejected_and_sampling_never_uses_future(self):
        row = {"fps": 60, "start_frame": 0, "end_frame": 61}
        indices = temporal_indices(row, 16)
        self.assertEqual(indices, list(range(30, 61, 2)))
        with self.assertRaises(ValueError):
            temporal_indices({"fps": 30, "start_frame": 0, "end_frame": 5}, 16)

    def test_bilinear_half_pixel_rgb_contract(self):
        rgb = np.zeros((2, 2, 3), dtype=np.uint8)
        rgb[0, 0, 0] = 255
        resized = preprocess_rgb(rgb, (0, 0, 1, 1), 3)
        self.assertAlmostEqual(float(resized[0, 1, 1]), 0.25, places=6)
        self.assertEqual(float(resized[1:].sum()), 0)

    def test_decoded_video_loader_is_causal_and_rejects_fixture_as_gameplay(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root/"synthetic-timeline.avi"
            writer = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"MJPG"), 30, (64, 64))
            self.assertTrue(writer.isOpened())
            for index in range(20):
                writer.write(np.full((64, 64, 3), index*10, dtype=np.uint8))
            writer.release()
            row = {"clip_id": "synthetic-clip", "source_id": "synthetic-source", "video_path": str(path),
                   "start_frame": 0, "end_frame": 16, "fps": 30, "roi": [0, 0, 1, 1],
                   "annotation_status": "reviewed", "reviewer": "synthetic-test", "example_only": True,
                   "labels": {"attack": True, "threat": None, "tti_censored": True, "impact_evidence": "NO_CONTACT"}}
            manifest = root/"samples.jsonl"
            manifest.write_text(json.dumps(row)+"\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                VideoSamples(manifest)
            frames, labels, masks, _ = VideoSamples(manifest, allow_synthetic=True, size=32)[0]
            self.assertEqual(tuple(frames.shape), (16, 3, 32, 32))
            self.assertAlmostEqual(float(frames[-1].mean()), 150/255, delta=2/255)
            self.assertAlmostEqual(float(frames[0].mean()), 0, delta=2/255)
            self.assertEqual(float(masks["threat"]), 0)
            self.assertEqual(float(masks["tti"]), 0)

    def test_causal_temporal_layers_ignore_future_values(self):
        torch.manual_seed(3)
        tcn = CausalBlock(16, 4).eval()
        x = torch.randn(2, 16, 16)
        changed = x.clone()
        changed[:, :, 8:] += 100
        torch.testing.assert_close(tcn(x)[:, :, :8], tcn(changed)[:, :, :8])
        attention = CausalAttention(16, 16).eval()
        x = torch.randn(2, 16, 16)
        changed = x.clone()
        changed[:, 8:] += 100
        torch.testing.assert_close(attention(x)[:, :8], attention(changed)[:, :8])

    def test_all_models_have_native_v2_output_contract_and_masked_loss(self):
        frames, labels, masks = synthetic_batch(count=2, frames=8, size=32)
        for architecture in ARCHITECTURES:
            with self.subTest(architecture=architecture):
                model = build_model(architecture, frames=8, width=16)
                outputs = model(frames)
                self.assertEqual([tuple(v.shape) for v in outputs],
                                 [(2, 1), (2, 1), (2, 1), (2, 1), (2, 9), (2, 14), (2, 5), (2, 8)])
                zeros = {key: torch.zeros_like(value) for key, value in masks.items()}
                loss, _ = multitask_loss(outputs, labels, zeros)
                self.assertEqual(float(loss.detach()), 0)
                loss.backward()
                self.assertTrue(all(p.grad is None or torch.count_nonzero(p.grad) == 0 for p in model.parameters()))

    def test_weapon_direction_not_inferred_from_player_dodge_or_missing_evidence(self):
        labels = {"direction": "RIGHT", "attack_direction": "RIGHT_TO_LEFT"}
        values, masks = target_values({"labels": labels})
        self.assertEqual(float(masks["direction"]), 1)
        self.assertEqual(float(masks["attack_direction"]), 0)
        labels.update(attack_direction_evidence="VISUAL_TRAJECTORY",
                      attack_direction_space="SCREEN_WITH_WOLF_REFERENCE")
        values, masks = target_values({"labels": labels})
        self.assertEqual(float(masks["attack_direction"]), 1)
        self.assertEqual(int(values["attack_direction"]), 1)
        self.assertEqual(int(values["direction"]), 1)
        labels["attack_direction"] = "UNKNOWN"
        self.assertEqual(float(target_values({"labels": labels})[1]["attack_direction"]), 0)

    def test_lucas_kanade_measures_translation_and_has_no_future_leakage(self):
        flow = CausalLucasKanade(size=40)
        torch.manual_seed(14)
        # Smooth textured image translated by one pixel to the right.
        texture = torch.nn.functional.avg_pool2d(torch.rand(1, 3, 40, 40), 3, 1, 1)
        frames = torch.stack((texture, torch.roll(texture, 1, -1), torch.roll(texture, 2, -1)), dim=1)
        measured = flow(frames)
        self.assertEqual(int(torch.count_nonzero(measured[:, 0])), 0)
        self.assertGreater(float(measured[:, 1:, 0, 6:-6, 6:-6].mean()), 0.65)
        self.assertLess(float(measured[:, 1:, 1, 6:-6, 6:-6].mean().abs()), 0.15)
        changed = frames.clone()
        changed[:, 2] = torch.rand_like(changed[:, 2])
        torch.testing.assert_close(flow(changed)[:, :2], measured[:, :2])
        self.assertEqual(int(torch.count_nonzero(flow(torch.ones_like(frames)))), 0)

    def test_legacy_v1_model_contract_remains_exportable(self):
        model = build_model("cnn_gru", frames=8, width=16, contract="temporal-v1")
        self.assertEqual(len(model(torch.zeros(1, 8, 3, 32, 32))), 7)
        self.assertFalse(any(name.startswith("heads.attack_direction") for name in model.state_dict()))

    def test_temporal_pretraining_does_not_invent_order_for_static_clips(self):
        model = build_model("optical_flow_fusion", frames=8, width=16)
        frames = torch.full((2, 8, 3, 32, 32), 0.4)
        without = contrastive_loss(model, frames, temporal_weight=0)
        with_order = contrastive_loss(model, frames, temporal_weight=0.25)
        torch.testing.assert_close(without, with_order)

    def test_reviewed_phase_cores_leave_gap_and_contact_unknown(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mapping = [{"source_frame": source_frame, "source_pts_ms": source_frame*1000/30,
                        "duplicated": False} for source_frame in range(90, 141)]
            (root/"frames.jsonl").write_text("".join(json.dumps(row)+"\n" for row in mapping))
            clip = {"clip_id": "unit-mapping-fixture", "source_id": "unit-source", "source_group_id": "g",
                    "player_id": "p", "session_id": "s", "source_sha256": "metadata-test-only",
                    "source_pts_path": str(root/"frames.jsonl"), "video_path": "not-decoded-by-mapping-test.mp4",
                    "frame_map_path": str(root/"frames.jsonl"), "fps_num": 30, "fps_den": 1,
                    "roi": [0, 0, 1, 1]}
            (root/"clips.jsonl").write_text(json.dumps(clip)+"\n")
            reviews = []
            for begin, end, state in ((100, 113, "ATTACK_WINDUP"), (117, 121, "ACTIVE_ATTACK")):
                reviews.append({"schema": "reviewed-phase-intervals-v1", "review_id": f"test-{begin}",
                    "source_id": "unit-source", "source_sha256": "metadata-test-only",
                    "start_source_frame": begin, "end_source_frame": end, "annotation_status": "reviewed",
                    "reviewer": "unit-test-fixture", "review_method": "SYNTHETIC_METADATA_TEST",
                    "evidence": "No real media involved in this mapping regression test",
                    "labels": {"state": state, "attack": True, "threat": None, "tti_ms": None,
                               "tti_censored": True, "impact_evidence": "OCCLUDED"}})
            (root/"reviews.jsonl").write_text("".join(json.dumps(row)+"\n" for row in reviews))
            export_intervals(root/"clips.jsonl", root/"reviews.jsonl", root/"samples.jsonl", frames=16)
            samples = [json.loads(line) for line in (root/"samples.jsonl").read_text().splitlines()]
            self.assertEqual({row["source_frame"] for row in samples}, set(range(105, 113))|set(range(117, 121)))
            self.assertTrue(all(row["labels"]["tti_ms"] is None and row["labels"]["threat"] is None for row in samples))

    def test_unsupported_head_logits_never_become_accuracy_evidence(self):
        row = {"attack_probability": 0.05, "threat_probability": 0.01,
               "labels": {"attack": False, "threat": False}}
        metrics = summarize_predictions([row], supported_heads={"attack_supported": True, "threat_supported": False})
        self.assertEqual(metrics["attack"]["tn"], 1)
        self.assertEqual(metrics["threat"]["available_label_count"], 1)
        self.assertEqual(metrics["threat"]["evaluable_samples"], 0)
        self.assertIsNone(metrics["threat"]["false_positive_fraction_of_negatives"])
        self.assertFalse(metrics["per_boss"]["UNKNOWN"]["threat"]["model_head_supported"])

    def test_hard_negative_and_pseudolabels_never_become_accepted(self):
        rows = [{"source_id": "a", "clip_id": "b", "attack_probability": 0.95,
                 "threat_probability": 0.94, "labels": {"threat": False}},
                {"source_id": "c", "clip_id": "d", "attack_probability": 0.93,
                 "threat_probability": 0.90, "labels": {}}]
        proposals = review_queue(rows)
        self.assertEqual(len(proposals), 2)
        self.assertTrue(all(p["annotation_status"] == "needs_review" and p["proposed_labels"] == {} for p in proposals))

    def test_repeated_detections_match_only_one_event_no_fake_impact(self):
        events = [{"source_id": "s", "reviewed": True, "windup_ms": 100, "active_end_ms": 300,
                   "impact_ms": None, "impact_evidence": "NO_CONTACT"}]
        detections = [{"source_id": "s", "source_ms": 180}, {"source_id": "s", "source_ms": 220}]
        result = evaluate_events(events, detections)
        self.assertEqual((result["tp"], result["fp"], result["fn"]), (1, 1, 0))
        self.assertIsNone(result["observed_impact_lead_p50_ms"])
        self.assertIsNone(result["false_dodges"])


if __name__ == "__main__":
    unittest.main()
