"""Import native JPEG frame bundles without inventing 15-FPS capture timestamps."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess

from DatasetTools.common import (append_jsonl, executable, load_jsonl, safe_id, sha256_file,
                                 tool_version, utc_now, write_json, write_jsonl)
from DatasetTools.downloader.ingest import probe_video


def import_recording(bundle: Path, root: Path, *, creator: str, usage_evidence: str,
                     boss: str = "UNKNOWN", source_id: str = "", make_clip: bool = True,
                     example_only: bool = False) -> dict:
    if not creator.strip() or not usage_evidence.strip():
        raise ValueError("Identify the recording owner and permitted use before import.")
    bundle, root = bundle.resolve(), root.resolve()
    manifest_path = bundle / "sample.json"
    if not manifest_path.is_file():
        raise ValueError("No final sample.json; the native recording may be incomplete.")
    recording = json.loads(manifest_path.read_text(encoding="utf-8"))
    if recording.get("schema_version") != 1 or recording.get("format") != "sekiro-jpeg-frame-bundle":
        raise ValueError("Unsupported native recording format.")
    session = safe_id(recording["session_id"])
    sample = safe_id(recording["sample_id"])
    identifier = safe_id(source_id or (session + "_" + sample))
    frames = recording.get("frames", [])
    if not 2 <= len(frames) <= 33:
        raise ValueError("A useful native sample needs 2–33 recorded frames, without padded posthistory.")
    first_generation = frames[0].get("generation")
    previous_pts, previous_sequence = -math.inf, -1
    for index, frame in enumerate(frames):
        name = frame.get("file", "")
        if frame.get("index") != index or not re.fullmatch(r"frame-\d{6}\.jpg", name):
            raise ValueError("Only generated relative JPEG names and contiguous frame indices are accepted.")
        if not (bundle / name).is_file():
            raise ValueError(f"Missing recorded frame: {name}")
        pts, sequence = frame["source_qpc_ms"], frame["sequence"]
        if not math.isfinite(pts) or pts <= previous_pts or sequence <= previous_sequence:
            raise ValueError("Capture timestamps and source sequence IDs must increase strictly.")
        if frame.get("generation") != first_generation:
            raise ValueError("A recording cannot combine capture generations.")
        previous_pts, previous_sequence = pts, sequence
    source_dir = root / "sources" / identifier
    if any(row["source_id"] == identifier for row in load_jsonl(root / "source_manifest.jsonl")):
        raise ValueError("Recording source ID already imported; choose a new version if intentionally revised.")
    source_dir.mkdir(parents=True, exist_ok=False)
    try:
        originals = source_dir / "original_bundle"
        originals.mkdir()
        shutil.copy2(manifest_path, originals / "sample.json")
        original_hashes = {"sample.json": sha256_file(manifest_path)}
        concat_lines = ["ffconcat version 1.0"]
        for index, frame in enumerate(frames):
            name = frame["file"]
            shutil.copy2(bundle / name, originals / name)
            original_hashes[name] = sha256_file(originals / name)
            concat_lines.extend([f"file '{name}'", "option framerate 1000"])
            if index + 1 < len(frames):
                concat_lines.append(f"duration {(frames[index + 1]['source_qpc_ms'] - frame['source_qpc_ms']) / 1000:.9f}")
        concat = originals / "frames.ffconcat"
        concat.write_text("\n".join(concat_lines) + "\n", encoding="utf-8")
        # The concat is regenerated from validated relative names. Never execute
        # arbitrary paths/directives from an externally edited concat file.
        media = source_dir / "source.mkv"
        subprocess.run([executable("ffmpeg"), "-v", "error", "-nostdin", "-f", "concat", "-safe", "0",
                        "-i", str(concat), "-fps_mode", "passthrough", "-c:v", "ffv1", "-threads", "1",
                        str(media)], capture_output=True, text=True, check=True, timeout=120)
        video, decoded_pts = probe_video(media)
        if len(decoded_pts) != len(frames):
            raise ValueError("Converted native recording changed frame count; refuse ambiguous source mapping.")
        # Encoded review timestamps are rounded/relative. The original native
        # QPC values remain the exact annotation and training time authority.
        pts = [{"source_frame": index, "source_pts_ms": frame["source_qpc_ms"],
                "capture_sequence": frame["sequence"], "capture_generation": frame["generation"],
                "duration_ms": frames[index + 1]["source_qpc_ms"] - frame["source_qpc_ms"]
                               if index + 1 < len(frames) else None,
                "duplicate_pts": False, "encoded_review_pts_ms": decoded_pts[index]["source_pts_ms"]}
               for index, frame in enumerate(frames)]
        pts_path = source_dir / "source_pts.jsonl"
        write_jsonl(pts_path, pts)
        video.update({"first_pts_ms": pts[0]["source_pts_ms"], "last_pts_ms": pts[-1]["source_pts_ms"],
                      "duration_ms": pts[-1]["source_pts_ms"] - pts[0]["source_pts_ms"],
                      "timeline_kind": "CAPTURE_QPC", "frame_count": len(frames)})
        source = {"schema_version": "2.0", "source_id": identifier, "source_group_id": session,
                  "player_id": creator.strip(), "creator": creator.strip(), "session_id": session,
                  "duplicate_group_id": session, "boss": boss, "url": "", "retrieved_url": "",
                  "retrieved_at": utc_now(), "usage_basis": "SYNTHETIC_TEST" if example_only else "OWN_RECORDING",
                  "usage_evidence": usage_evidence, "license_or_usage_note": "Local owner-provided app recording",
                  "example_only": example_only, "media_path": str(media), "pts_path": str(pts_path),
                  "sha256": sha256_file(media), "pts_sha256": sha256_file(pts_path),
                  "original_bundle_hashes": original_hashes,
                  "original_bundle_sha256": hashlib.sha256(json.dumps(original_hashes, sort_keys=True).encode()).hexdigest(),
                  "video": video, "decoder": tool_version("ffprobe"),
                  "download_status": "IMPORTED", "processing_status": "PTS_INDEXED",
                  "native_recording": {"git_commit": recording.get("git_commit"), "marker": recording.get("marker"),
                                       "timing": recording.get("timing"), "annotation_status": "proposed"}}
        write_json(source_dir / "source.json", source)
        append_jsonl(root / "source_manifest.jsonl", source)
    except Exception:
        shutil.rmtree(source_dir, ignore_errors=True)
        raise
    if make_clip:
        from DatasetTools.clip_miner.mine import Candidate, extract_candidates

        # This is already a triggered 1s-before/1s-after sample, so it need not
        # pass the long-video motion miner. Truncated samples remain truncated.
        first, last = pts[0]["source_pts_ms"], pts[-1]["source_pts_ms"]
        count = math.floor((last - first) * 30 / 1000) + 1
        end = first + count * 1000 / 30
        candidate = Candidate(first, end, recording["marker"]["qpc_ms"],
                              "NATIVE_MARKER_REVIEW_" + recording["marker"]["reason"], 0.0)
        clips = extract_candidates(source, [candidate], root / "clips", fps=30, max_width=1280)
        for clip in clips:
            clip["native_marker"] = recording["marker"]
            clip["available_recording_timing"] = recording.get("timing")
            write_json(Path(clip["video_path"]).with_suffix(".json"), clip)
            append_jsonl(root / "clips.jsonl", clip)
        source["created_review_clips"] = [clip["clip_id"] for clip in clips]
    return source


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path("data"))
    parser.add_argument("--creator", required=True)
    parser.add_argument("--usage-evidence", required=True)
    parser.add_argument("--boss", default="UNKNOWN")
    parser.add_argument("--source-id", default="")
    parser.add_argument("--no-review-clip", action="store_true")
    args = parser.parse_args()
    result = import_recording(args.bundle, args.root, creator=args.creator, usage_evidence=args.usage_evidence,
                               boss=args.boss, source_id=args.source_id, make_clip=not args.no_review_clip)
    print(json.dumps({"source_id": result["source_id"], "frames": result["video"]["frame_count"],
                      "timing": "ORIGINAL_CAPTURE_QPC", "review_required": True,
                      "clips": result.get("created_review_clips", [])}))


if __name__ == "__main__":
    main()
