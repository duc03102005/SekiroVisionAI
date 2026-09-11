"""Causal, time-based sampling from DatasetTools.annotation.export_samples."""
import math
from pathlib import Path
import random

import cv2
import numpy as np
import torch
from torch.nn import functional as F
from torch.utils.data import Dataset

from .splits import read_jsonl

STATES = ("IDLE", "WALK", "RUN", "TURN", "ATTACK_WINDUP", "ACTIVE_ATTACK",
          "RECOVERY", "COMBO_CONTINUATION", "FEINT")
CLASSES = ("HORIZONTAL_SLASH", "VERTICAL_SLASH", "DIAGONAL_SLASH", "THRUST", "SWEEP", "GRAB",
           "JUMP_ATTACK", "OVERHEAD_ATTACK", "SPIN_ATTACK", "PROJECTILE", "CHARGE", "AOE",
           "MULTI_HIT_COMBO", "UNKNOWN_ATTACK")
DIRECTIONS = ("LEFT", "RIGHT", "FORWARD", "BACK", "NEUTRAL")
ATTACK_DIRECTIONS = ("LEFT_TO_RIGHT", "RIGHT_TO_LEFT", "TOP_TO_BOTTOM", "BOTTOM_TO_TOP",
                     "TOWARD_WOLF", "AWAY_FROM_WOLF", "RADIAL", "UNKNOWN")
DEFAULT_ROI = (0.28, 0.12, 0.72, 0.61)


def preprocess_rgb(rgb, roi=DEFAULT_ROI, size=320):
    """ROI floor/ceil, RGB/255, bilinear half-pixel; matches native contract."""
    h, w = rgb.shape[:2]
    left, top, right, bottom = roi
    if not (0 <= left < right <= 1 and 0 <= top < bottom <= 1):
        raise ValueError(f"Invalid ROI: {roi}")
    crop = rgb[math.floor(top*h):math.ceil(bottom*h), math.floor(left*w):math.ceil(right*w)]
    tensor = torch.from_numpy(np.ascontiguousarray(crop)).permute(2, 0, 1).float().div_(255)
    return F.interpolate(tensor.unsqueeze(0), size=(size, size), mode="bilinear", align_corners=False)[0]


def temporal_indices(row, count, sample_interval_ms=1000/30):
    fps = float(row["fps"])
    start, end = int(row["start_frame"]), int(row["end_frame"])
    if fps <= 0 or not math.isfinite(fps) or start < 0 or end <= start:
        raise ValueError("Invalid FPS or clip-local sample bounds")
    anchor = end - 1
    indices = [math.floor(anchor-(count-1-index)*sample_interval_ms*fps/1000+1e-7) for index in range(count)]
    if indices[0] < start:
        raise ValueError(f"Insufficient causal history for {row.get('clip_id')}: need {count} samples")
    return indices


def target_values(row):
    labels = row.get("labels", {})
    values, masks = {}, {}
    for key in ("attack", "threat"):
        value = labels.get(key)
        if value is not None and type(value) is not bool:
            raise ValueError(f"{key} must be true, false or null")
        values[key], masks[key] = float(value or 0), float(value is not None)
    for key, classes in (("state", STATES), ("class", CLASSES), ("direction", DIRECTIONS)):
        value = labels.get(key)
        if value is not None and value not in classes:
            raise ValueError(f"Unknown {key}: {value!r}")
        values[key] = classes.index(value) if value is not None else 0
        masks[key] = float(value is not None)
    trajectory = labels.get("attack_direction")
    if trajectory is not None and trajectory not in ATTACK_DIRECTIONS:
        raise ValueError(f"Unknown attack_direction: {trajectory!r}")
    valid_trajectory = (trajectory not in (None, "UNKNOWN") and
                        labels.get("attack_direction_evidence") == "VISUAL_TRAJECTORY" and
                        labels.get("attack_direction_space") == "SCREEN_WITH_WOLF_REFERENCE")
    values["attack_direction"] = ATTACK_DIRECTIONS.index(trajectory) if valid_trajectory else 7
    masks["attack_direction"] = float(valid_trajectory)
    tti = labels.get("tti_ms")
    supported = (tti is not None and not labels.get("tti_censored", True)
                 and labels.get("impact_evidence") == "OBSERVED_CONTACT")
    if supported and (not math.isfinite(float(tti)) or not 0 <= float(tti) <= 3000):
        raise ValueError("Observed TTI must be finite and within the documented 3000ms horizon")
    values["tti"], masks["tti"] = float(tti) if supported else 0.0, float(supported)
    return ({key: torch.tensor(value, dtype=torch.long if key in ("state", "class", "direction", "attack_direction") else torch.float32)
             for key, value in values.items()}, {key: torch.tensor(value) for key, value in masks.items()})


class VideoSamples(Dataset):
    def __init__(self, manifest, sources=None, frames=16, size=320, augment=False,
                 allow_synthetic=False, require_review=True):
        self.manifest = Path(manifest)
        self.rows = [row for row in read_jsonl(manifest) if sources is None or row["source_id"] in sources]
        self.frames, self.size, self.augment = frames, size, augment
        for row in self.rows:
            if require_review and (row.get("annotation_status") != "reviewed" or not row.get("reviewer")):
                raise ValueError("Supervised learning requires explicit reviewed annotations")
            if row.get("example_only", False) and not allow_synthetic:
                raise ValueError("Synthetic fixtures are forbidden in a gameplay training run")
            temporal_indices(row, frames)
            target_values(row)

    def __len__(self):
        return len(self.rows)

    def __getitem__(self, index):
        row = self.rows[index]
        indices = temporal_indices(row, self.frames)
        path = Path(row["video_path"])
        if not path.is_absolute():
            path = self.manifest.parent/path
        reader = cv2.VideoCapture(str(path))
        try:
            if not reader.isOpened():
                raise OSError(f"Cannot decode {path}")
            reader.set(cv2.CAP_PROP_POS_FRAMES, indices[0])
            decoded, wanted = {}, set(indices)
            quality = random.choice((55, 70, 85, 100)) if self.augment else 100
            for frame_index in range(indices[0], indices[-1]+1):
                ok, bgr = reader.read()
                if not ok:
                    raise OSError(f"Truncated clip at {frame_index}: {path}")
                if frame_index in wanted:
                    if quality < 100:
                        success, encoded = cv2.imencode(".jpg", bgr, [cv2.IMWRITE_JPEG_QUALITY, quality])
                        if success:
                            bgr = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
                    decoded[frame_index] = preprocess_rgb(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), row.get("roi", DEFAULT_ROI), self.size)
            frames = torch.stack([decoded[item] for item in indices])
        finally:
            reader.release()
        if self.augment:
            # Same photometric/spatial transform for the entire sequence: no
            # time warp, flip, future interpolation, or invented direction label.
            frames = (frames*random.uniform(0.8, 1.2)+random.uniform(-0.06, 0.06)).clamp_(0, 1)
            if random.random() < 0.25:
                reduced = random.choice((self.size//2, self.size*3//4))
                frames = F.interpolate(F.interpolate(frames, size=(reduced, reduced), mode="bilinear", align_corners=False),
                                       size=(self.size, self.size), mode="bilinear", align_corners=False)
            if random.random() < 0.15:
                frames = F.avg_pool2d(F.pad(frames, (1, 1, 1, 1), mode="replicate"), 3, stride=1)
        values, masks = target_values(row)
        return frames, values, masks, index
