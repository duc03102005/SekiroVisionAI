"""Import permitted media and preserve its actual decoded frame timestamps.

Example:
    python -m DatasetTools.downloader.ingest --input C:/recordings/fight.mp4 \
      --source-id LOCAL_001 --creator me --session-id session_001 \
      --boss "Genichiro Ashina" --usage-basis OWN_RECORDING \
      --usage-evidence "Recorded by me; permitted project analysis" --root data
"""

from __future__ import annotations

import argparse
from fractions import Fraction
import json
import math
from pathlib import Path
import shutil
import subprocess
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener

from DatasetTools.common import (append_jsonl, executable, load_jsonl, safe_id,
                                 sha256_file, tool_version, utc_now, write_json,
                                 write_jsonl)

USAGE_BASES = ("OWN_RECORDING", "EXPLICIT_PERMISSION", "OPEN_LICENSE", "SYNTHETIC_TEST")
MEDIA_SUFFIXES = {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v"}
REFERENCE_PLATFORMS = ("youtube.com", "youtu.be", "twitch.tv", "bilibili.com",
                       "reddit.com", "redd.it", "steamcommunity.com")


def permitted_direct_url(url: str) -> str:
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("Direct media downloads require an HTTPS URL without credentials.")
    host = parsed.hostname.lower()
    if any(host == domain or host.endswith("." + domain) for domain in REFERENCE_PLATFORMS):
        raise ValueError("Platform URLs belong in the reference catalog. Use a permitted export/local file.")
    if Path(parsed.path).suffix.lower() not in MEDIA_SUFFIXES:
        raise ValueError("Expected a direct video file URL; watch pages and stream extraction are unsupported.")
    return url


class _HttpsRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        # CDN redirects may have opaque paths, but never downgrade or accept embedded credentials.
        parsed = urlparse(new_url)
        if parsed.scheme != "https" or parsed.username or parsed.password:
            raise ValueError("Rejected a non-HTTPS or credential-bearing redirect.")
        host = (parsed.hostname or "").lower()
        if any(host == domain or host.endswith("." + domain) for domain in REFERENCE_PLATFORMS):
            raise ValueError("Redirect led to a reference-only platform.")
        return super().redirect_request(request, response, code, message, headers, new_url)


def download_direct(url: str, destination: Path, max_bytes: int) -> str:
    permitted_direct_url(url)
    opener = build_opener(_HttpsRedirects())
    request = Request(url, headers={"User-Agent": "SekiroVisionAI-dataset/0.2 permitted-media-import"})
    try:
        with opener.open(request, timeout=60) as response, destination.open("xb") as stream:
            length = response.headers.get("Content-Length")
            if length and int(length) > max_bytes:
                raise ValueError("Media exceeds --max-bytes; nothing was imported.")
            received = 0
            for block in iter(lambda: response.read(1024 * 1024), b""):
                received += len(block)
                if received > max_bytes:
                    raise ValueError("Download exceeded --max-bytes; partial media removed.")
                stream.write(block)
            return response.geturl()
    except Exception:
        destination.unlink(missing_ok=True)
        raise


def probe_video(path: Path) -> tuple[dict, list[dict]]:
    command = [executable("ffprobe"), "-v", "error", "-select_streams", "v:0",
               "-show_entries",
               "stream=codec_name,width,height,avg_frame_rate,r_frame_rate,time_base,start_time,duration,nb_frames:format=duration",
               "-of", "json", str(path)]
    metadata = json.loads(subprocess.run(command, capture_output=True, text=True,
                                         check=True, timeout=120).stdout)
    streams = metadata.get("streams", [])
    if len(streams) != 1 or not streams[0].get("width") or not streams[0].get("height"):
        raise ValueError("No decodable video stream found.")
    video = streams[0]
    # Compact output avoids holding ffprobe's full per-frame JSON in memory. The
    # map itself is modest (timestamp metadata only), not decoded video frames.
    frame_command = [executable("ffprobe"), "-v", "error", "-select_streams", "v:0",
                     "-show_frames", "-show_entries",
                     "frame=best_effort_timestamp_time,pkt_duration_time",
                     "-of", "compact=p=0:nk=0", str(path)]
    process = subprocess.Popen(frame_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, encoding="utf-8")
    assert process.stdout is not None
    rows = []
    previous = -math.inf
    try:
        for line in process.stdout:
            fields = dict(part.split("=", 1) for part in line.strip().split("|") if "=" in part)
            value = fields.get("best_effort_timestamp_time")
            if value is None:
                # Side-data continuation lines are not video frames.
                continue
            pts_ms = float(value) * 1000
            if not math.isfinite(pts_ms) or pts_ms < previous - 0.001:
                raise ValueError("Source has missing/non-monotonic decoded PTS; repair explicitly, then re-ingest.")
            duration = fields.get("pkt_duration_time")
            rows.append({"source_frame": len(rows), "source_pts_ms": pts_ms,
                         "duration_ms": float(duration) * 1000 if duration not in (None, "N/A") else None,
                         "duplicate_pts": abs(pts_ms - previous) < 0.001})
            previous = pts_ms
        stderr = process.stderr.read() if process.stderr else ""
        if process.wait() != 0:
            raise RuntimeError(f"ffprobe frame scan failed: {stderr[-2000:]}")
    except Exception:
        process.kill()
        process.wait()
        raise
    finally:
        process.stdout.close()
        if process.stderr is not None:
            process.stderr.close()
    if not rows:
        raise ValueError("No actual source timestamps found. Nominal FPS will not be substituted for PTS.")
    try:
        average = Fraction(video.get("avg_frame_rate", "0/1"))
    except (ValueError, ZeroDivisionError):
        # Some VFR containers deliberately report 0/0. Preserve unknown nominal
        # cadence; actual source PTS above remains the authoritative timeline.
        average = Fraction(0, 1)
    video.update({"frame_count": len(rows), "fps_num": average.numerator,
                  "fps_den": average.denominator,
                  "first_pts_ms": rows[0]["source_pts_ms"],
                  "last_pts_ms": rows[-1]["source_pts_ms"],
                  "duration_ms": float(metadata.get("format", {}).get("duration", 0)) * 1000})
    return video, rows


def ingest(*, input_path: str, source_id: str, creator: str, session_id: str,
           boss: str, usage_basis: str, usage_evidence: str, root: Path,
           source_url: str = "", license_note: str = "", duplicate_group_id: str = "",
           max_bytes: int = 8 * 1024 ** 3) -> dict:
    safe_id(source_id)
    safe_id(session_id)
    if usage_basis not in USAGE_BASES or not usage_evidence.strip() or not creator.strip():
        raise ValueError("Record creator, usage basis and actual permission/license evidence before importing media.")
    root = root.resolve()
    manifest = root / "source_manifest.jsonl"
    if any(row.get("source_id") == source_id for row in load_jsonl(manifest)):
        raise ValueError(f"Source ID already exists: {source_id}. Use a new version/ID; sources are immutable.")
    is_url = input_path.startswith(("https://", "http://"))
    if is_url:
        permitted_direct_url(input_path)
        if usage_basis == "SYNTHETIC_TEST":
            raise ValueError("SYNTHETIC_TEST imports must be local generated fixtures.")
        suffix = Path(urlparse(input_path).path).suffix.lower()
    else:
        original = Path(input_path).expanduser().resolve()
        if not original.is_file() or original.suffix.lower() not in MEDIA_SUFFIXES:
            raise ValueError("Input must be an existing local video, or an explicitly permitted direct HTTPS video.")
        if original.stat().st_size > max_bytes:
            raise ValueError("Local input exceeds --max-bytes.")
        suffix = original.suffix.lower()
    source_dir = root / "sources" / source_id
    source_dir.mkdir(parents=True, exist_ok=False)
    media = source_dir / ("source" + suffix)
    try:
        final_url = download_direct(input_path, media, max_bytes) if is_url else ""
        if not is_url:
            shutil.copy2(original, media)
        metadata, pts = probe_video(media)
        pts_path = source_dir / "source_pts.jsonl"
        write_jsonl(pts_path, pts)
        record = {
            "schema_version": "2.0", "source_id": source_id,
            "source_group_id": session_id, "player_id": creator.strip(),
            "creator": creator.strip(), "session_id": session_id,
            "duplicate_group_id": duplicate_group_id,
            "boss": boss, "url": source_url or (input_path if is_url else ""),
            "retrieved_url": final_url, "retrieved_at": utc_now(),
            "usage_basis": usage_basis, "usage_evidence": usage_evidence.strip(),
            "license_or_usage_note": license_note, "example_only": usage_basis == "SYNTHETIC_TEST",
            "media_path": str(media), "pts_path": str(pts_path),
            "sha256": sha256_file(media), "pts_sha256": sha256_file(pts_path),
            "video": metadata, "decoder": tool_version("ffprobe"),
            "download_status": "IMPORTED", "processing_status": "PTS_INDEXED",
        }
        write_json(source_dir / "source.json", record)
        append_jsonl(manifest, record)
        return record
    except Exception:
        # This directory was created exclusively by this import, so failed
        # ingestion cannot erase an existing recording or source version.
        shutil.rmtree(source_dir, ignore_errors=True)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True)
    parser.add_argument("--source-id", required=True)
    parser.add_argument("--creator", required=True)
    parser.add_argument("--session-id", required=True)
    parser.add_argument("--boss", default="UNKNOWN")
    parser.add_argument("--usage-basis", choices=USAGE_BASES, required=True)
    parser.add_argument("--usage-evidence", required=True)
    parser.add_argument("--source-url", default="")
    parser.add_argument("--license-note", default="")
    parser.add_argument("--duplicate-group-id", default="")
    parser.add_argument("--root", type=Path, default=Path("data"))
    parser.add_argument("--max-bytes", type=int, default=8 * 1024 ** 3)
    args = parser.parse_args()
    result = ingest(input_path=args.input, source_id=args.source_id, creator=args.creator,
                    session_id=args.session_id, boss=args.boss, usage_basis=args.usage_basis,
                    usage_evidence=args.usage_evidence, root=args.root, source_url=args.source_url,
                    license_note=args.license_note, duplicate_group_id=args.duplicate_group_id,
                    max_bytes=args.max_bytes)
    print(json.dumps({"source_id": result["source_id"], "sha256": result["sha256"],
                      "frames": result["video"]["frame_count"], "example_only": result["example_only"]}))


if __name__ == "__main__":
    main()
