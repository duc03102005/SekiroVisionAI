"""Check source hashes, monotonic PTS maps, reviewed labels and split leakage."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from DatasetTools.common import load_jsonl, sha256_file
from DatasetTools.annotation.contract import validate_annotation
from DatasetTools.annotation.journal import latest_annotations
from DatasetTools.validation.splits import validate_splits


def validate_dataset(sources_path: Path, clips_path: Path, annotations_path: Path | None = None,
                     splits_path: Path | None = None, verify_hashes: bool = False) -> dict:
    sources = load_jsonl(sources_path)
    by_id = {row["source_id"]: row for row in sources}
    if len(by_id) != len(sources):
        raise ValueError("Duplicate source IDs.")
    for source in sources:
        if source.get("usage_basis") not in ("OWN_RECORDING", "OPEN_LICENSE", "EXPLICIT_PERMISSION", "SYNTHETIC_TEST"):
            raise ValueError("A reference-only URL cannot be treated as acquired training media.")
        if not source.get("usage_evidence"):
            raise ValueError("Imported source has no usage evidence.")
        if not Path(source["media_path"]).is_file() or not Path(source["pts_path"]).is_file():
            raise ValueError("Imported media or original PTS index is missing.")
        if verify_hashes and sha256_file(Path(source["media_path"])) != source["sha256"]:
            raise ValueError("Original media hash changed.")
    clips = load_jsonl(clips_path)
    by_clip = {clip["clip_id"]: clip for clip in clips}
    if len(by_clip) != len(clips):
        raise ValueError("Duplicate clip IDs.")
    for clip in clips:
        source = by_id.get(clip["source_id"])
        if source is None:
            raise ValueError("Clip refers to an unregistered source.")
        if bool(clip.get("example_only")) != bool(source.get("example_only")):
            raise ValueError("Synthetic provenance was lost while deriving a clip.")
        if not Path(clip["video_path"]).is_file():
            raise ValueError("Derived video is missing.")
        if verify_hashes and sha256_file(Path(clip["video_path"])) != clip["sha256"]:
            raise ValueError("Derived video hash changed.")
        original = load_jsonl(Path(source["pts_path"]))
        mapping = load_jsonl(Path(clip["frame_map_path"]))
        if len(mapping) != clip["frame_count"]:
            raise ValueError("Frame map length mismatch.")
        previous_pts, previous_source = -float("inf"), None
        for index, frame in enumerate(mapping):
            source_index = frame["source_frame"]
            if frame["frame_index"] != index or not 0 <= source_index < len(original):
                raise ValueError("Frame map has invalid indices.")
            if abs(frame["source_pts_ms"] - original[source_index]["source_pts_ms"]) > 0.001:
                raise ValueError("Frame map PTS was inferred/altered instead of preserving the source observation.")
            if frame["source_pts_ms"] < previous_pts or frame["source_pts_ms"] > frame["target_source_pts_ms"] + 0.001:
                raise ValueError("Frame map is non-monotonic or looks into the future.")
            if bool(frame["duplicated"]) != (previous_source == source_index) or frame.get("interpolated") is not False:
                raise ValueError("Duplicated/interpolated observation flags are wrong.")
            previous_pts, previous_source = frame["source_pts_ms"], source_index
    annotations = latest_annotations(annotations_path) if annotations_path else []
    for row in annotations:
        if row["clip_id"] not in by_clip:
            raise ValueError("Annotation has no clip manifest.")
        validate_annotation(row, by_clip[row["clip_id"]])
    if splits_path:
        validate_splits(sources, json.loads(splits_path.read_text(encoding="utf-8")))
    return {"sources": len(sources), "clips": len(clips), "annotations": len(annotations),
            "reviewed_annotations": sum(row["annotation_status"] == "reviewed" for row in annotations),
            "synthetic_sources": sum(bool(source.get("example_only")) for source in sources),
            "source_hashes_verified": verify_hashes, "leakage_checked": bool(splits_path)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=Path("data/source_manifest.jsonl"))
    parser.add_argument("--clips", type=Path, default=Path("data/clips.jsonl"))
    parser.add_argument("--annotations", type=Path)
    parser.add_argument("--splits", type=Path)
    parser.add_argument("--verify-hashes", action="store_true")
    args = parser.parse_args()
    print(json.dumps(validate_dataset(args.sources, args.clips, args.annotations, args.splits, args.verify_hashes)))


if __name__ == "__main__":
    main()
