"""Map visually reviewed certain phase cores to causal training samples.

An interval is a set of confidently observed frames, not a claim that its edge
is the exact onset of an attack. Gaps remain unknown. Contact timing, attack
direction and threat intersection are separately reviewed fields, never copied
from an attack label. This complements the full-event annotation journal.
"""
import argparse
import json
from pathlib import Path

from DatasetTools.common import load_jsonl, sha256_file, write_json, write_jsonl
from Training.datasets.video_samples import STATES, target_values


def export(clips_path, reviews_path, output, frames=16):
    if frames not in (8, 16, 24, 32, 48):
        raise ValueError("Unsupported temporal history")
    output = Path(output)
    if output.exists():
        raise FileExistsError("Reviewed sample manifests are versioned")
    clips = load_jsonl(Path(clips_path))
    reviews = load_jsonl(Path(reviews_path))
    samples = {}
    for review in reviews:
        if (review.get("schema") != "reviewed-phase-intervals-v1" or
                review.get("annotation_status") != "reviewed" or not review.get("reviewer") or
                not review.get("review_method") or not review.get("evidence")):
            raise ValueError("Review needs explicit identity, method, status and visual evidence")
        begin, end = review["start_source_frame"], review["end_source_frame"]
        if type(begin) is not int or type(end) is not int or not 0 <= begin < end:
            raise ValueError("Certain phase intervals use half-open original source-frame bounds")
        labels = review["labels"]
        if labels.get("state") not in (None, *STATES):
            raise ValueError("Observed state must be known, or null to preserve uncertainty")
        if labels.get("tti_ms") is not None:
            raise ValueError("Never copy a fixed TTI across an interval; provide a reviewed observed_contact_source_frame")
        contact_frame = review.get("observed_contact_source_frame")
        if contact_frame is not None and (type(contact_frame) is not int or contact_frame < 0 or
                labels.get("impact_evidence") != "OBSERVED_CONTACT"):
            raise ValueError("Point contact requires separately reviewed original source-frame evidence")
        target_values({"labels": labels})
        matched = [clip for clip in clips if clip["source_id"] == review["source_id"]]
        if not matched:
            raise ValueError("No normalized media for reviewed source")
        for clip in matched:
            if clip["source_sha256"] != review["source_sha256"] or clip.get("example_only"):
                raise ValueError("Review/media provenance mismatch or synthetic source")
            if clip["fps_num"] != 30 or clip["fps_den"] != 1:
                raise ValueError("Use the canonical causal30Hz normalization")
            mapping = load_jsonl(Path(clip["frame_map_path"]))
            contact_pts = None
            if contact_frame is not None:
                original_pts = load_jsonl(Path(clip["source_pts_path"]))
                contact_pts = next((item["source_pts_ms"] for item in original_pts
                                    if item["source_frame"] == contact_frame), None)
                if contact_pts is None:
                    raise ValueError("Reviewed point contact has no original PTS mapping")
            for anchor in range(frames-1, len(mapping)):
                if not begin <= mapping[anchor]["source_frame"] < end:
                    continue
                row = {
                    "clip_id": clip["clip_id"], "source_id": clip["source_id"],
                    "source_group_id": clip["source_group_id"], "player_id": clip["player_id"],
                    "session_id": clip["session_id"], "creator": clip.get("creator"),
                    "source_sha256": clip["source_sha256"], "source_url": clip.get("source_url"),
                    "duplicate_group_id": clip.get("duplicate_group_id"), "boss": review.get("boss", clip.get("boss", "UNKNOWN")),
                    "video_path": clip["video_path"], "fps": 30,
                    "start_frame": anchor+1-frames, "end_frame": anchor+1,
                    "source_frame": mapping[anchor]["source_frame"],
                    "source_pts_ms": mapping[anchor]["source_pts_ms"],
                    "roi": review.get("roi", clip["roi"]), "labels": dict(labels),
                    "annotation_id": review["review_id"], "annotation_status": "reviewed",
                    "reviewer": review["reviewer"], "review_method": review["review_method"],
                    "annotation_confidence": review.get("confidence"), "example_only": False,
                    "duplicated_observations": sum(item["duplicated"] for item in mapping[anchor+1-frames:anchor+1]),
                }
                if contact_pts is not None and row["source_pts_ms"] <= contact_pts:
                    row["labels"]["tti_ms"] = contact_pts-row["source_pts_ms"]
                    row["labels"]["tti_censored"] = False
                target_values(row)
                key = (clip["clip_id"], anchor)
                if key in samples and samples[key]["labels"] != row["labels"]:
                    raise ValueError("Contradicting reviewed phase cores")
                samples[key] = row
    result = [samples[key] for key in sorted(samples)]
    write_jsonl(output, result)
    summary = {"review_count": len(reviews), "sample_count": len(result),
               "review_specs_sha256": sha256_file(reviews_path), "samples_sha256": sha256_file(output),
               "source_ids": sorted({row["source_id"] for row in result}),
               "states": {state: sum(row["labels"]["state"] == state for row in result) for state in STATES},
               "unknown_state_samples": sum(row["labels"].get("state") is None for row in result),
               "review_methods": sorted({row["review_method"] for row in result}),
               "observed_contact_samples": sum(row["labels"].get("tti_ms") is not None and
                    row["labels"].get("impact_evidence") == "OBSERVED_CONTACT" for row in result),
               "release_quality_claim": None}
    write_json(output.with_suffix(".summary.json"), summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("clips", type=Path)
    parser.add_argument("reviews", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    args = parser.parse_args()
    print(json.dumps(export(args.clips, args.reviews, args.output, args.frames)))


if __name__ == "__main__":
    main()
