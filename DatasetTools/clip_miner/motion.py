"""Background affine estimation plus local dense-flow residuals.

The ROI is a combat *candidate*, not a recognized boss or weapon. All outputs
are mining/debug signals, never calibrated attack probabilities.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
import math

import cv2
import numpy as np


@dataclass
class MotionObservation:
    source_frame: int
    source_pts_ms: float
    local_px_s: float = 0.0
    camera_px_s: float = 0.0
    residual_difference: float = 0.0
    acceleration_px_s2: float = 0.0
    background_inlier_fraction: float = 0.0
    score: float = 0.0
    camera_only: bool = False
    scene_cut: bool = False
    valid: bool = False
    reason: str = "WARMUP"

    def to_dict(self) -> dict:
        return asdict(self)


def check_roi(roi) -> tuple[float, float, float, float]:
    if len(roi) != 4 or not all(math.isfinite(float(value)) for value in roi):
        raise ValueError("ROI must contain four finite normalized values.")
    left, top, right, bottom = map(float, roi)
    if not (0 <= left < right <= 1 and 0 <= top < bottom <= 1 and
            right - left >= 0.08 and bottom - top >= 0.08):
        raise ValueError("ROI must be normalized [left, top, right, bottom] with usable area.")
    return left, top, right, bottom


class CameraCompensatedMotion:
    def __init__(self, roi=(0.20, 0.12, 0.82, 0.82), width: int = 384):
        self.roi = check_roi(roi)
        self.width = width
        self.previous = None
        self.previous_pts = None
        self.previous_local = 0.0

    def update(self, bgr: np.ndarray, frame_index: int, pts_ms: float) -> MotionObservation:
        height = max(32, round(bgr.shape[0] * self.width / bgr.shape[1]))
        gray = cv2.cvtColor(cv2.resize(bgr, (self.width, height), interpolation=cv2.INTER_AREA),
                            cv2.COLOR_BGR2GRAY)
        result = MotionObservation(frame_index, pts_ms)
        previous, old_pts = self.previous, self.previous_pts
        self.previous, self.previous_pts = gray, pts_ms
        if previous is None or previous.shape != gray.shape or old_pts is None:
            return result
        dt = (pts_ms - old_pts) / 1000
        if not 0.005 <= dt <= 0.8:
            result.reason = "TIMESTAMP_GAP"
            self.previous_local = 0
            return result
        h, w = gray.shape
        x0, y0, x1, y1 = (round(self.roi[0] * w), round(self.roi[1] * h),
                          round(self.roi[2] * w), round(self.roi[3] * h))
        foreground = np.zeros_like(gray)
        foreground[y0:y1, x0:x1] = 255
        background = np.full_like(gray, 255)
        background[:max(2, h // 20)] = 0
        background[-max(2, h // 10):] = 0
        background[:, :max(2, w // 30)] = 0
        background[:, -max(2, w // 30):] = 0
        background[y0:y1, x0:x1] = 0
        points = cv2.goodFeaturesToTrack(previous, maxCorners=240, qualityLevel=0.01,
                                        minDistance=7, mask=background, blockSize=5)
        if points is None or len(points) < 12:
            result.reason = "INSUFFICIENT_BACKGROUND_TEXTURE"
            self.previous_local = 0
            return result
        next_points, status, error = cv2.calcOpticalFlowPyrLK(
            previous, gray, points, None, winSize=(21, 21), maxLevel=3,
            criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 30, 0.01))
        if next_points is None or status is None:
            result.reason = "BACKGROUND_TRACK_LOST"
            return result
        keep = status.reshape(-1).astype(bool)
        if error is not None:
            keep &= error.reshape(-1) < 30
        before = points.reshape(-1, 2)[keep]
        after = next_points.reshape(-1, 2)[keep]
        if len(before) < 12:
            result.reason = "BACKGROUND_TRACK_LOST"
            result.scene_cut = float(np.mean(cv2.absdiff(previous, gray))) > 40
            return result
        matrix, inliers = cv2.estimateAffinePartial2D(
            before, after, method=cv2.RANSAC, ransacReprojThreshold=2.5,
            maxIters=1000, confidence=0.99, refineIters=10)
        result.background_inlier_fraction = float(np.mean(inliers)) if inliers is not None else 0
        if matrix is None or result.background_inlier_fraction < 0.45:
            result.reason = "CAMERA_ESTIMATE_UNCERTAIN"
            result.scene_cut = float(np.mean(cv2.absdiff(previous, gray))) > 40
            self.previous_local = 0
            return result
        scale = float(np.linalg.norm(matrix[0, :2]))
        if not 0.8 < scale < 1.2 or abs(matrix[0, 2]) > w * 0.4 or abs(matrix[1, 2]) > h * 0.4:
            result.reason = "CAMERA_JUMP"
            result.scene_cut = True
            self.previous_local = 0
            return result
        warped = cv2.warpAffine(previous, matrix, (w, h), flags=cv2.INTER_LINEAR,
                               borderMode=cv2.BORDER_REFLECT101)
        difference = cv2.absdiff(warped, gray)
        background_error = float(np.median(difference[background > 0]))
        result.scene_cut = background_error > 35
        if result.scene_cut:
            result.reason = "SCENE_CUT_OR_FLASH"
            self.previous_local = 0
            return result
        # The flow is in PREVIOUS-frame coordinates, so subtract the camera
        # transform at each previous-frame pixel, rather than a single vector.
        flow = cv2.calcOpticalFlowFarneback(previous, gray, None, 0.5, 3, 15, 3, 5, 1.2, 0)
        yy, xx = np.mgrid[:h, :w].astype(np.float32)
        camera_x = matrix[0, 0] * xx + matrix[0, 1] * yy + matrix[0, 2] - xx
        camera_y = matrix[1, 0] * xx + matrix[1, 1] * yy + matrix[1, 2] - yy
        residual = np.hypot(flow[:, :, 0] - camera_x, flow[:, :, 1] - camera_y)
        background_noise = float(np.percentile(residual[background > 0], 75))
        local = max(0.0, float(np.percentile(residual[foreground > 0], 90)) - background_noise)
        result.local_px_s = local / dt
        result.camera_px_s = float(np.median(np.hypot(camera_x, camera_y))) / dt
        result.residual_difference = max(0.0, float(np.mean(difference[y0:y1, x0:x1])) - background_error)
        result.acceleration_px_s2 = (result.local_px_s - self.previous_local) / dt
        self.previous_local = result.local_px_s
        result.score = float(min(1.0, 0.65 * min(1.0, result.local_px_s / 45.0)
                                 + 0.25 * min(1.0, result.residual_difference / 22.0)
                                 + 0.10 * min(1.0, max(0.0, result.acceleration_px_s2) / 300.0)))
        result.camera_only = result.camera_px_s > 5 and result.local_px_s < 6 and result.residual_difference < 6
        if result.camera_only:
            result.score = 0.0
        result.valid = True
        result.reason = "CAMERA_ONLY" if result.camera_only else "LOCAL_MOTION"
        return result
