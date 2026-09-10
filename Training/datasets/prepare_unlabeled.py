"""Prepare causal representation-learning windows from permission-cleared video.

Reuses the production dataset decoder's floor-PTS normalization. No labels are
inferred from motion, game identity, a player's successful action or source title.
All original source/player/session groups and media hashes travel with samples.
"""
import argparse
import json
from pathlib import Path

from DatasetTools.common import load_jsonl, sha256_file, write_json, write_jsonl
from DatasetTools.clip_miner.mine import Candidate, extract_candidates


def prepare(sources_path, output, frames=16, stride=8, max_width=1280, selections_path=None):
    sources = load_jsonl(Path(sources_path))
    selections = load_jsonl(Path(selections_path)) if selections_path else None
    if selections is not None:
        known = {source["source_id"] for source in sources}
        if any(selection.get("source_id") not in known for selection in selections):
            raise ValueError("Selected interval refers to a source outside the imported manifest")
        sources = [source for source in sources if any(row["source_id"] == source["source_id"] for row in selections)]
    if not sources or frames not in (8, 16, 24, 32, 48) or stride < 1:
        raise ValueError("Need imported sources, a supported history length and positive stride")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    clips, samples = [], []
    for source in sources:
        if source.get("example_only") or source.get("usage_basis") not in (
                "OWN_RECORDING", "EXPLICIT_PERMISSION", "OPEN_LICENSE"):
            raise ValueError("Real pretraining requires non-synthetic permission-cleared source manifests")
        if source.get("download_status") != "IMPORTED" or not source.get("usage_evidence"):
            raise ValueError("Source must be locally imported with recorded usage evidence")
        media = Path(source["media_path"])
        if sha256_file(media) != source["sha256"]:
            raise ValueError("Source media hash changed after acquisition")
        timeline = load_jsonl(Path(source["pts_path"]))
        if len(timeline) < 2:
            continue
        # End just after the final observation, without extending source video.
        start, end = timeline[0]["source_pts_ms"], timeline[-1]["source_pts_ms"] + 0.001
        spans = [row for row in selections if row["source_id"] == source["source_id"]] if selections is not None else [
            {"start_ms": start, "end_ms": end}]
        if any(not start <= row["start_ms"] < row["end_ms"] <= end for row in spans):
            raise ValueError("Selected interval must lie inside the actual source PTS extent")
        candidates = [Candidate(row["start_ms"], row["end_ms"], row["start_ms"], "UNLABELED_REPRESENTATION", 0) for row in spans]
        records = extract_candidates(source, candidates,
                                     output/"clips", fps=30, max_width=max_width, roi=(0.10, 0.05, 0.90, 0.87))
        clips.extend(records)
        for clip in records:
            mapping = load_jsonl(Path(clip["frame_map_path"]))
            for anchor in range(frames-1, clip["frame_count"], stride):
                selected = mapping[anchor+1-frames:anchor+1]
                samples.append({
                    "clip_id": clip["clip_id"], "source_id": source["source_id"],
                    "source_group_id": source["source_group_id"], "player_id": source["player_id"],
                    "session_id": source["session_id"], "creator": source.get("creator"),
                    "duplicate_group_id": source.get("duplicate_group_id"), "boss": source.get("boss", "UNKNOWN"),
                    "source_sha256": source["sha256"], "source_url": source.get("url", ""),
                    "usage_basis": source["usage_basis"], "usage_evidence": source["usage_evidence"],
                    "video_path": clip["video_path"], "fps": 30, "roi": clip["roi"],
                    "start_frame": anchor+1-frames, "end_frame": anchor+1,
                    "source_pts_ms": mapping[anchor]["source_pts_ms"],
                    "source_frame": mapping[anchor]["source_frame"], "labels": {},
                    "annotation_status": "unreviewed", "example_only": False,
                    "duplicated_observations": sum(item["duplicated"] for item in selected),
                })
    write_jsonl(output/"clips.jsonl", clips)
    write_jsonl(output/"unlabeled_samples.jsonl", samples)
    summary = {"purpose": "self-supervised representation learning only", "supervised_labels": 0,
               "source_manifest_sha256": sha256_file(sources_path), "source_count": len(sources),
               "source_groups": sorted({source["source_group_id"] for source in sources}),
               "clip_count": len(clips), "sample_count": len(samples), "history_length": frames,
               "stride": stride, "normalization": "30Hz causal floor-PTS frames; no future interpolation"}
    if selections_path:
        summary["selected_intervals_sha256"] = sha256_file(selections_path)
        summary["selected_intervals"] = selections
    write_json(output/"preparation.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--stride", type=int, default=8)
    parser.add_argument("--max-width", type=int, default=1280)
    parser.add_argument("--selections", type=Path, help="Optional JSONL source_id/start_ms/end_ms clean gameplay intervals")
    args = parser.parse_args()
    print(json.dumps(prepare(args.sources, args.output, args.frames, args.stride, args.max_width, args.selections)))


if __name__ == "__main__":
    main()
