"""Mine 2–4 second review candidates and extract clips with exact PTS maps."""

from __future__ import annotations

import argparse
from bisect import bisect_right
from dataclasses import dataclass
import json
import math
from pathlib import Path
import subprocess
import tempfile
from statistics import median

import cv2

from DatasetTools.common import (append_jsonl, executable, load_jsonl, safe_id,
                                 sha256_file, tool_version, utc_now, write_json,
                                 write_jsonl)
from DatasetTools.clip_miner.motion import CameraCompensatedMotion, check_roi


@dataclass
class Candidate:
    start_ms: float
    end_ms: float
    anchor_ms: float
    reason: str
    score: float


def scan_video(source: dict, *, analysis_fps: float = 5, roi=(0.20, 0.12, 0.82, 0.82)) -> list[dict]:
    if not 1 <= analysis_fps <= 30:
        raise ValueError("analysis_fps must be between 1 and 30.")
    timeline = load_jsonl(Path(source["pts_path"]))
    cap = cv2.VideoCapture(source["media_path"])
    if not cap.isOpened():
        raise RuntimeError("OpenCV could not decode the imported source.")
    detector = CameraCompensatedMotion(roi)
    next_analysis = timeline[0]["source_pts_ms"]
    output = []
    try:
        for index, frame_time in enumerate(timeline):
            if not cap.grab():
                raise ValueError(f"Decoder/PTS frame count disagrees at source frame {index}.")
            pts = frame_time["source_pts_ms"]
            if pts + 0.001 < next_analysis:
                continue
            okay, frame = cap.retrieve()
            if not okay:
                raise RuntimeError(f"Could not decode source frame {index}.")
            output.append(detector.update(frame, index, pts).to_dict())
            next_analysis = pts + 1000 / analysis_fps
        if cap.grab():
            raise ValueError("Decoder returned more frames than the stored PTS index.")
    finally:
        cap.release()
    return output


def propose_windows(observations: list[dict], start_ms: float, end_ms: float, *,
                    duration_s: float = 3.0, max_clips: int = 120, threshold: float = 0.48,
                    negative_fraction: float = 0.4) -> list[Candidate]:
    if not 2 <= duration_s <= 4 or not 0 <= negative_fraction <= 1 or max_clips < 1:
        raise ValueError("Use 2–4 second clips, a valid negative fraction, and positive max_clips.")
    duration_ms = duration_s * 1000
    if end_ms - start_ms < duration_ms:
        return []
    positives = sorted((item for item in observations if item["valid"] and not item["camera_only"]
                        and not item["scene_cut"] and item["score"] >= threshold),
                       key=lambda item: (-item["score"], item["source_pts_ms"]))
    negative_candidates = [item for item in observations if item["scene_cut"] or
                           (item["valid"] and (item["camera_only"] or
                                               (item["local_px_s"] < 4 and item["score"] < 0.15)))]
    # Camera/cut negatives are more informative than a very large idle dump.
    negative_candidates.sort(key=lambda item: (not item["scene_cut"], not item["camera_only"],
                                                item["source_pts_ms"]))
    selected: list[Candidate] = []

    def add(item: dict, reason: str) -> bool:
        center = item["source_pts_ms"]
        start = min(max(start_ms, center - duration_ms / 3), end_ms - duration_ms)
        end = start + duration_ms
        if any(max(0, min(end, other.end_ms) - max(start, other.start_ms)) > duration_ms * 0.15
               for other in selected):
            return False
        selected.append(Candidate(start, end, center, reason, float(item["score"])))
        return True

    positive_limit = max(1, round(max_clips * (1 - negative_fraction)))
    for item in positives:
        if len(selected) >= positive_limit:
            break
        add(item, "LOCAL_MOTION_CANDIDATE")
    for item in negative_candidates:
        if len(selected) >= max_clips:
            break
        reason = ("SCENE_CUT_REVIEW" if item["scene_cut"] else
                  "CAMERA_MOTION_REVIEW" if item["camera_only"] else "LOW_MOTION_REVIEW")
        add(item, reason)
    return sorted(selected, key=lambda candidate: candidate.start_ms)


def frame_mapping(timeline: list[dict], candidate: Candidate, fps: int = 30) -> list[dict]:
    if fps < 1 or fps > 120:
        raise ValueError("Output FPS must be 1–120.")
    source_times = [row["source_pts_ms"] for row in timeline]
    count = round((candidate.end_ms - candidate.start_ms) * fps / 1000)
    mapping = []
    previous_index = None
    for index in range(count):
        requested = candidate.start_ms + index * 1000 / fps
        source_index = bisect_right(source_times, requested + 1e-6) - 1
        if source_index < 0:
            raise ValueError("Candidate starts before the first actual source PTS.")
        original = timeline[source_index]
        mapping.append({"frame_index": index, "clip_pts_ms": index * 1000 / fps,
                        "target_source_pts_ms": requested,
                        "source_frame": original["source_frame"],
                        "source_pts_ms": original["source_pts_ms"],
                        "duplicated": source_index == previous_index, "interpolated": False})
        previous_index = source_index
    return mapping


class _Encoder:
    def __init__(self, path: Path, width: int, height: int, fps: int):
        self.path = path
        self.temporary = path.with_name(path.stem + ".part.mp4")
        self.stderr = tempfile.TemporaryFile()
        command = [executable("ffmpeg"), "-v", "error", "-nostdin", "-y",
                   "-f", "rawvideo", "-pixel_format", "bgr24", "-video_size", f"{width}x{height}",
                   "-framerate", str(fps), "-i", "pipe:0", "-an", "-c:v", "libx264",
                   "-threads", "1", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p",
                   "-movflags", "+faststart", str(self.temporary)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                        stderr=self.stderr)

    def write(self, frame) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(frame.tobytes())

    def close(self, *, failed: bool = False) -> None:
        if self.process.stdin is not None:
            try:
                self.process.stdin.close()
            except BrokenPipeError:
                failed = True
            self.process.stdin = None
        if failed:
            self.process.kill()
        code = self.process.wait(timeout=60)
        self.stderr.seek(0)
        errors = self.stderr.read().decode("utf-8", "replace")
        self.stderr.close()
        if failed or code:
            self.temporary.unlink(missing_ok=True)
            if not failed:
                raise RuntimeError(f"Clip encode failed: {errors[-2000:]}")
        else:
            self.temporary.replace(self.path)


def extract_candidates(source: dict, candidates: list[Candidate], output_dir: Path, *,
                       fps: int = 30, max_width: int = 1280, roi=(0.20, 0.12, 0.82, 0.82)) -> list[dict]:
    """Decode once; emit exactly the causal source frames described by each map.

    A generated CFR frame uses the newest original observation at/before its
    requested PTS. Duplicates are explicit; no interpolation invents motion.
    """
    check_roi(roi)
    if max_width != 0 and max_width < 64:
        raise ValueError("max_width must be at least 64, or 0 to retain source resolution.")
    timeline = load_jsonl(Path(source["pts_path"]))
    output_dir.mkdir(parents=True, exist_ok=True)
    requests: dict[int, list[tuple[int, int]]] = {}
    records: list[dict] = []
    maps: list[list[dict]] = []
    for index, candidate in enumerate(candidates):
        identifier = safe_id(f"{source['source_id']}_{round(candidate.start_ms):010d}_{round(candidate.end_ms):010d}")
        path = output_dir / f"{identifier}.mp4"
        if path.exists() or path.with_suffix(".frames.jsonl").exists():
            raise ValueError(f"Clip version exists: {identifier}; use a new output directory.")
        mapping = frame_mapping(timeline, candidate, fps)
        if not mapping:
            continue
        maps.append(mapping)
        for entry in mapping:
            requests.setdefault(entry["source_frame"], []).append((index, entry["frame_index"]))
        records.append({"schema_version": "2.0", "clip_id": identifier,
                        "source_id": source["source_id"], "source_group_id": source["source_group_id"],
                        "player_id": source["player_id"], "session_id": source["session_id"],
                        "creator": source.get("creator", source["player_id"]), "source_url": source.get("url", ""),
                        "duplicate_group_id": source.get("duplicate_group_id", ""),
                        "source_sha256": source["sha256"], "boss": source["boss"],
                        "source_pts_path": source["pts_path"],
                        "video_path": str(path.resolve()),
                        "frame_map_path": str(path.with_suffix(".frames.jsonl").resolve()),
                        "start_source_frame": mapping[0]["source_frame"],
                        "end_source_frame": mapping[-1]["source_frame"] + 1,
                        "source_start_ms": candidate.start_ms, "source_end_ms": candidate.end_ms,
                        "fps_num": fps, "fps_den": 1, "frame_count": len(mapping),
                        "roi": list(roi), "proposal_reason": candidate.reason,
                        "proposal_anchor_ms": candidate.anchor_ms,
                        "proposal_score": candidate.score, "annotation_status": "proposed",
                        "example_only": bool(source.get("example_only", False)),
                        "normalization": "CFR; causal floor-PTS sampling; no interpolation; H264 CRF18",
                        "encoder": tool_version("ffmpeg"), "created_at": utc_now()})
    if not records:
        return []
    cap = cv2.VideoCapture(source["media_path"])
    if not cap.isOpened():
        raise RuntimeError("Could not open source video for extraction.")
    active: dict[int, _Encoder] = {}
    completed: set[int] = set()
    try:
        for frame_index in range(max(requests) + 1):
            if not cap.grab():
                raise ValueError(f"Source decoder ended before indexed frame {frame_index}.")
            selected = requests.get(frame_index)
            if not selected:
                continue
            okay, frame = cap.retrieve()
            if not okay:
                raise RuntimeError(f"Could not retrieve indexed source frame {frame_index}.")
            scale = min(1, max_width / frame.shape[1]) if max_width else 1.0
            width = max(2, round(frame.shape[1] * scale) // 2 * 2)
            height = max(2, round(frame.shape[0] * scale) // 2 * 2)
            if (width, height) != (frame.shape[1], frame.shape[0]):
                frame = cv2.resize(frame, (width, height), interpolation=cv2.INTER_AREA)
            for clip_index, output_index in selected:
                record = records[clip_index]
                if clip_index not in active:
                    active[clip_index] = _Encoder(Path(record["video_path"]), width, height, fps)
                    record["width"], record["height"] = width, height
                active[clip_index].write(frame)
                if output_index + 1 == record["frame_count"]:
                    active.pop(clip_index).close()
                    record["sha256"] = sha256_file(Path(record["video_path"]))
                    write_jsonl(Path(record["frame_map_path"]), maps[clip_index])
                    write_json(Path(record["video_path"]).with_suffix(".json"), record)
                    completed.add(clip_index)
        if len(completed) != len(records):
            raise RuntimeError("Not every candidate clip was encoded.")
    except Exception:
        for encoder in active.values():
            encoder.close(failed=True)
        # Completed clips have immutable sidecars and can be recovered manually;
        # the aggregate manifest is appended only after the entire batch succeeds.
        raise
    finally:
        cap.release()
    return records


def mine_source(source: dict, output_dir: Path, manifest: Path, *, analysis_fps: float = 5,
                duration_s: float = 3, max_clips: int = 120, threshold: float = 0.48,
                fps: int = 30, max_width: int = 1280, roi=(0.20, 0.12, 0.82, 0.82)) -> dict:
    observations = scan_video(source, analysis_fps=analysis_fps, roi=roi)
    source_video = source["video"]
    timeline = load_jsonl(Path(source["pts_path"]))
    last = timeline[-1]
    recent_deltas = [later["source_pts_ms"] - earlier["source_pts_ms"]
                     for earlier, later in zip(timeline[-31:-1], timeline[-30:])
                     if later["source_pts_ms"] > earlier["source_pts_ms"]]
    final_duration = last.get("duration_ms") or (median(recent_deltas) if recent_deltas else 0)
    candidates = propose_windows(observations, timeline[0]["source_pts_ms"],
                                 last["source_pts_ms"] + final_duration,
                                 duration_s=duration_s, max_clips=max_clips, threshold=threshold)
    output_dir.mkdir(parents=True, exist_ok=True)
    signals_path = output_dir / f"{source['source_id']}.motion.jsonl"
    write_jsonl(signals_path, observations)
    clips = extract_candidates(source, candidates, output_dir, fps=fps, max_width=max_width, roi=roi)
    known = {row["clip_id"] for row in load_jsonl(manifest)}
    for clip in clips:
        if clip["clip_id"] in known:
            raise ValueError("Aggregate manifest already includes this clip ID.")
        append_jsonl(manifest, clip)
    return {"source_id": source["source_id"], "observations": len(observations), "clips": len(clips),
            "review_required": True, "example_only": source.get("example_only", False),
            "motion_signals": str(signals_path.resolve())}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=Path("data/source_manifest.jsonl"))
    parser.add_argument("--source-id", required=True)
    parser.add_argument("--output", type=Path, default=Path("data/clips"))
    parser.add_argument("--manifest", type=Path, default=Path("data/clips.jsonl"))
    parser.add_argument("--analysis-fps", type=float, default=5)
    parser.add_argument("--clip-seconds", type=float, default=3)
    parser.add_argument("--max-clips", type=int, default=120)
    parser.add_argument("--threshold", type=float, default=0.48)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--max-width", type=int, default=1280)
    parser.add_argument("--roi", type=float, nargs=4, default=[0.20, 0.12, 0.82, 0.82])
    args = parser.parse_args()
    source = next((row for row in load_jsonl(args.sources) if row["source_id"] == args.source_id), None)
    if source is None:
        parser.error("source-id is absent from the imported source manifest")
    print(json.dumps(mine_source(source, args.output, args.manifest, analysis_fps=args.analysis_fps,
                                  duration_s=args.clip_seconds, max_clips=args.max_clips,
                                  threshold=args.threshold, fps=args.fps, max_width=args.max_width,
                                  roi=args.roi)))


if __name__ == "__main__":
    main()
