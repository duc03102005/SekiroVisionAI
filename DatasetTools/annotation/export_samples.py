"""Export reviewed temporal annotations into the Training JSONL interface."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from DatasetTools.common import load_jsonl, sha256_file, utc_now, write_json, write_jsonl
from DatasetTools.annotation.contract import state_at, validate_annotation
from DatasetTools.annotation.journal import latest_annotations


def export_samples(clips_path: Path, annotations_path: Path, output: Path, *,
                   sequence_length: int = 16, stride: int = 3, allow_synthetic: bool = False) -> dict:
    if sequence_length not in (8, 16, 24, 32, 48) or stride < 1:
        raise ValueError("Use sequence length 8/16/24/32/48 and a positive stride.")
    if output.exists():
        raise ValueError("Sample manifests are versioned; choose a new output file.")
    clips = {clip["clip_id"]: clip for clip in load_jsonl(clips_path)}
    samples: dict[tuple[str, int], dict] = {}
    reviewed_events = 0
    ignored = {"unreviewed": 0, "synthetic": 0}
    for annotation in latest_annotations(annotations_path):
        if annotation["annotation_status"] != "reviewed":
            ignored["unreviewed"] += 1
            continue
        clip = clips.get(annotation["clip_id"])
        if clip is None:
            raise ValueError(f"Missing clip for annotation {annotation['annotation_id']}")
        validate_annotation(annotation, clip)
        if annotation.get("example_only") and not allow_synthetic:
            ignored["synthetic"] += 1
            continue
        reviewed_events += 1
        mapping = load_jsonl(Path(clip["frame_map_path"]))
        if len(mapping) != clip["frame_count"]:
            raise ValueError("Clip frame map length differs from its manifest.")
        pts_by_source = {entry["source_frame"]: entry["source_pts_ms"] for entry in mapping}
        if clip.get("source_pts_path"):
            pts_by_source.update({entry["source_frame"]: entry["source_pts_ms"]
                                  for entry in load_jsonl(Path(clip["source_pts_path"]))})
        impact_pts = pts_by_source.get(annotation.get("impact_frame"))
        if annotation["impact_evidence"] == "OBSERVED_CONTACT" and impact_pts is None:
            raise ValueError("Observed contact source frame has no actual PTS mapping; never infer nominal-FPS TTI.")
        for anchor_index in range(sequence_length - 1, len(mapping), stride):
            anchor = mapping[anchor_index]
            state = state_at(annotation, anchor["source_frame"])
            if state is None:
                continue
            is_attack = state in ("ATTACK_WINDUP", "ACTIVE_ATTACK", "COMBO_CONTINUATION")
            # Event-level danger labels refer to the reviewed attacking phase.
            # Recovery danger would need a separate explicit reviewed interval
            # (e.g. a still-travelling projectile), so leave it unknown here.
            threat = (False if annotation["scope"] == "ENTIRE_CLIP_NON_THREAT" else
                      annotation.get("threat_label") if is_attack else None)
            tti_ms = None
            if (impact_pts is not None and annotation["impact_evidence"] == "OBSERVED_CONTACT"
                    and anchor["source_pts_ms"] <= impact_pts and is_attack):
                tti_ms = impact_pts - anchor["source_pts_ms"]
            dodge = annotation.get("dodge_start")
            direction = (annotation["dodge_direction"] if dodge is not None and
                         anchor["source_frame"] <= dodge and annotation["dodge_direction"] != "UNKNOWN" else None)
            sample = {
                "clip_id": clip["clip_id"], "source_id": clip["source_id"],
                "source_group_id": clip["source_group_id"], "boss": annotation["boss"],
                "player_id": clip["player_id"], "session_id": clip["session_id"],
                "creator": clip.get("creator", clip["player_id"]),
                "source_sha256": clip.get("source_sha256", ""), "source_url": clip.get("source_url", ""),
                "duplicate_group_id": clip.get("duplicate_group_id", ""),
                "video_path": clip["video_path"], "start_frame": anchor_index + 1 - sequence_length,
                "end_frame": anchor_index + 1, "fps": clip["fps_num"] / clip["fps_den"],
                "roi": annotation["roi"], "source_pts_ms": anchor["source_pts_ms"],
                "source_frame": anchor["source_frame"],
                "labels": {"state": state, "class": annotation["attack_type"] if is_attack else None,
                           "attack": is_attack, "threat": threat, "tti_ms": tti_ms,
                           "tti_censored": tti_ms is None, "impact_evidence": annotation["impact_evidence"],
                           "direction": direction, "direction_label_kind": "OBSERVED_RESPONSE" if direction else None},
                "annotation_status": "reviewed", "reviewer": annotation["reviewer"],
                "annotation_id": annotation["annotation_id"], "annotation_revision": annotation["revision"],
                "annotation_confidence": annotation["confidence"],
                "example_only": bool(annotation.get("example_only", False)),
                "duplicated_observations": sum(bool(entry["duplicated"]) for entry in
                                               mapping[anchor_index + 1 - sequence_length:anchor_index + 1]),
            }
            key = (clip["clip_id"], anchor_index)
            if key in samples and samples[key]["labels"] != sample["labels"]:
                raise ValueError(f"Conflicting reviewed strikes at {key}; resolve annotation overlap before export.")
            samples[key] = sample
    ordered = [samples[key] for key in sorted(samples)]
    write_jsonl(output, ordered)
    summary = {"schema_version": "2.0", "created_at": utc_now(), "samples": len(ordered),
               "reviewed_annotations": reviewed_events, "ignored": ignored,
               "sequence_length": sequence_length, "stride": stride,
               "source_count": len({row["source_id"] for row in ordered}),
               "bosses": sorted({row["boss"] for row in ordered}),
               "observed_contact_tti_samples": sum(row["labels"]["tti_ms"] is not None for row in ordered),
               "direction_semantics": "Observed player response; not a verified safe-action policy",
               "all_example_only": bool(ordered) and all(row["example_only"] for row in ordered),
               "clips_sha256": sha256_file(clips_path), "annotations_sha256": sha256_file(annotations_path),
               "samples_sha256": sha256_file(output)}
    write_json(output.with_suffix(".summary.json"), summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clips", type=Path, default=Path("data/clips.jsonl"))
    parser.add_argument("--annotations", type=Path, default=Path("data/annotations.jsonl"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sequence-length", type=int, choices=(8, 16, 24, 32, 48), default=16)
    parser.add_argument("--stride", type=int, default=3)
    parser.add_argument("--allow-synthetic-test-fixtures", action="store_true")
    args = parser.parse_args()
    print(json.dumps(export_samples(args.clips, args.annotations, args.output,
                                    sequence_length=args.sequence_length, stride=args.stride,
                                    allow_synthetic=args.allow_synthetic_test_fixtures)))


if __name__ == "__main__":
    main()
