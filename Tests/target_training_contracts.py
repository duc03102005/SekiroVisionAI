import unittest
import torch
from Training.targets.model import RoleDetector, GRID_H, GRID_W
from Training.targets.train import loss_value, validate_group_splits, decode


class TargetTrainingContracts(unittest.TestCase):
    def test_unknown_role_contributes_no_training_gradient(self):
        raw = torch.zeros(1, 2, 5, GRID_H, GRID_W, requires_grad=True)
        heat = torch.zeros(1, 2, GRID_H, GRID_W)
        positive = torch.zeros_like(heat)
        heat[0, 0, 10, 20] = positive[0, 0, 10, 20] = 1
        boxes = torch.zeros(1, 2, 4, GRID_H, GRID_W)
        boxes[0, 0, :, 10, 20] = torch.tensor([0.3, 0.3, 0.2, 0.4])
        reviewed = torch.tensor([1., 0.]).reshape(1, 2, 1, 1)
        loss_value(raw, heat, boxes, positive, reviewed).backward()
        self.assertEqual(float(raw.grad[:, 1].abs().sum()), 0)
        self.assertGreater(float(raw.grad[:, 0].abs().sum()), 0)

    def test_same_unknown_publisher_family_cannot_be_split_by_video(self):
        sources = {
            "a": {"source_id": "a", "source_group_id": "session-a", "creator": "Markov contributor unknown player"},
            "b": {"source_id": "b", "source_group_id": "session-b", "creator": "Markov contributor unknown player"},
        }
        with self.assertRaisesRegex(ValueError, "leakage"):
            validate_group_splits([{"source_id": "a", "split": "train"}, {"source_id": "b", "split": "test"}], sources)
        validate_group_splits([{"source_id": "a", "split": "train"}, {"source_id": "b", "split": "train"}], sources)

    def test_duplicate_media_hash_cannot_cross_partitions(self):
        sources = {"a": {"source_id": "a", "sha256": "same-bytes"}, "b": {"source_id": "b", "sha256": "same-bytes"}}
        with self.assertRaisesRegex(ValueError, "leakage"):
            validate_group_splits([{"source_id": "a", "split": "train"}, {"source_id": "b", "split": "val"}], sources)

    def test_dense_output_is_bounded_and_nms_keeps_roles_separate(self):
        torch.set_num_threads(2)
        model = RoleDetector().eval()
        with torch.no_grad():
            scores, boxes = model(torch.rand(1, 3, 192, 320))
        self.assertEqual(tuple(scores.shape), (1, 2, GRID_H, GRID_W))
        self.assertEqual(tuple(boxes.shape), (1, 2, 4, GRID_H, GRID_W))
        self.assertTrue(bool(torch.all(boxes >= 0) and torch.all(boxes <= 1)))
        scores.zero_(); scores[0, :, 10, 20] = .95
        scores[0, :, 10, 21] = .90
        boxes[0, :, :, :, :] = torch.tensor([.3, .2, .6, .7])[None, :, None, None]
        detections = decode(scores[0].numpy(), boxes[0].numpy())
        self.assertEqual([d["role"] for d in detections], ["Wolf", "Enemy"])


if __name__ == "__main__":
    unittest.main()
