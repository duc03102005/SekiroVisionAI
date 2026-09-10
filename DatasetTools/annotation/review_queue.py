"""Rank model disagreements/high scores for review; never auto-label negatives."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from DatasetTools.common import load_jsonl, utc_now, write_jsonl
from DatasetTools.annotation.journal import latest_annotations


def make_review_queue(predictions: list[dict], clips: list[dict], annotations: list[dict] = (), *,
                      high_threshold: float = 0.85, limit: int = 500) -> list[dict]:
    if not 0.5 < high_threshold <= 1 or limit < 1:
        raise ValueError("Use a high-confidence threshold in (0.5, 1] and positive queue limit.")
    known_clips = {clip["clip_id"]: clip for clip in clips}
    reviewed_negative = {row["clip_id"] for row in annotations if
                         row["annotation_status"] == "reviewed" and row["scope"] == "ENTIRE_CLIP_NON_THREAT"}
    queue: dict[str, dict] = {}
    for prediction in predictions:
        clip_id = prediction["clip_id"]
        if clip_id not in known_clips:
            raise ValueError("Prediction refers to an unknown extracted clip.")
        value = prediction.get("threat_probability")
        if not isinstance(value, (float, int)) or not 0 <= value <= 1:
            raise ValueError("Prediction threat_probability must be finite and in [0,1].")
        if not prediction.get("model_version"):
            raise ValueError("Record the actual model version that produced each proposal.")
        uncertainty = 1 - abs(2 * value - 1)
        if clip_id in reviewed_negative and value >= high_threshold:
            reason, priority = "REVIEWED_NEGATIVE_MODEL_DISAGREEMENT", 3 + value
        elif value >= high_threshold:
            reason, priority = "HIGH_THREAT_UNREVIEWED", 1 + value
        elif uncertainty >= 0.8:
            reason, priority = "UNCERTAIN_MODEL_PREDICTION", 2 + uncertainty
        else:
            continue
        row = {"clip_id": clip_id, "source_id": known_clips[clip_id]["source_id"],
               "model_version": prediction["model_version"], "reason": reason,
               "priority": priority, "prediction": prediction,
               "suggested_label": "NON_THREAT" if clip_id in reviewed_negative else None,
               "annotation_status": "proposed", "review_required": True,
               "example_only": bool(known_clips[clip_id].get("example_only", False)),
               "created_at": utc_now()}
        if clip_id not in queue or priority > queue[clip_id]["priority"]:
            queue[clip_id] = row
    return sorted(queue.values(), key=lambda row: (-row["priority"], row["clip_id"]))[:limit]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predictions", required=True, type=Path)
    parser.add_argument("--clips", type=Path, default=Path("data/clips.jsonl"))
    parser.add_argument("--annotations", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--high-threshold", type=float, default=0.85)
    parser.add_argument("--limit", type=int, default=500)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Review queues are versioned; choose a new output file")
    rows = make_review_queue(load_jsonl(args.predictions), load_jsonl(args.clips),
                              latest_annotations(args.annotations) if args.annotations else [],
                              high_threshold=args.high_threshold, limit=args.limit)
    write_jsonl(args.output, rows)
    print(json.dumps({"review_candidates": len(rows), "automatic_ground_truth_labels": 0}))


if __name__ == "__main__":
    main()
