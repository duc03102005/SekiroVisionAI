"""Convert an exact contiguous decoded video interval to portable native replay input.

Build/development tool only. Windows ReplayHarness reads MP4 directly. Pixels stay
at their source dimensions; no FPS conversion, invented timestamps, or labels.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
from decimal import Decimal
from pathlib import Path


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def read_exact(stream, size: int) -> bytes:
    chunks, remaining = [], size
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start-frame", type=int, default=0)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--max-output-mib", type=int, default=2048)
    args = parser.parse_args()
    if args.start_frame < 0 or not 1 <= args.frames <= 1_000_000 or args.max_output_mib < 1:
        parser.error("Invalid bounded frame/output limits")
    if args.output.exists():
        parser.error("Output already exists; choose a new path")
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        parser.error("This optional Linux developer conversion requires FFmpeg and FFprobe")
    info = json.loads(subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                       "stream=width,height:stream_side_data=rotation", "-of", "json", str(args.video)],
                       check=True, capture_output=True, text=True, timeout=60).stdout)
    stream_info = info["streams"][0]
    width, height = stream_info["width"], stream_info["height"]
    if not (16 <= width <= 8192 and 16 <= height <= 8192 and width * height <= 3840 * 2160):
        parser.error("Source dimensions exceed native 4K replay contract")
    if any(item.get("rotation", 0) != 0 for item in stream_info.get("side_data_list", [])):
        parser.error("Rotated source requires an explicit annotation transform; refusing silent rotation")
    frame_bytes = width * height * 4
    if 16 + args.frames * (24 + frame_bytes) > args.max_output_mib * 1024 * 1024:
        parser.error("Requested decoded interval exceeds output limit; reduce --frames or explicitly increase --max-output-mib")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    partial = args.output.with_name(args.output.name + ".partial")
    if partial.exists():
        parser.error("A partial output already exists; choose a new path")
    first, last, count = None, None, 0
    with tempfile.TemporaryFile() as decode_error, tempfile.TemporaryFile() as probe_error:
        decoder = subprocess.Popen([ffmpeg, "-nostdin", "-v", "error", "-noautorotate", "-i", str(args.video),
            "-map", "0:v:0", "-an", "-vf", f"select=between(n\\,{args.start_frame}\\,{args.start_frame + args.frames - 1})",
            "-frames:v", str(args.frames), "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"],
            stdout=subprocess.PIPE, stderr=decode_error)
        probe = subprocess.Popen([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
            "frame=best_effort_timestamp_time", "-of", "csv=p=0", str(args.video)], stdout=subprocess.PIPE, stderr=probe_error)
        try:
            assert decoder.stdout is not None and probe.stdout is not None
            source_index = 0
            with partial.open("xb") as output:
                output.write(b"SVRRAW01" + struct.pack("<II", width, height))
                for line in probe.stdout:
                    value = line.strip().split(b",")[0]
                    if not value:
                        continue
                    pts = int((Decimal(value.decode("ascii")) * 10_000_000).to_integral_value())
                    if source_index < args.start_frame:
                        source_index += 1
                        continue
                    pixels = read_exact(decoder.stdout, frame_bytes)
                    if len(pixels) != frame_bytes:
                        raise RuntimeError("Decoded pixel frames do not match original FFprobe presentation timestamps")
                    if last is not None and pts <= last:
                        raise RuntimeError("Nonmonotonic source PTS; no timestamp repair is permitted")
                    if first is None:
                        first = pts
                    last = pts
                    count += 1
                    output.write(struct.pack("<qQQ", pts, 1, source_index + 1))
                    output.write(pixels)
                    source_index += 1
                    if count >= args.frames:
                        break
            if count != args.frames:
                raise RuntimeError("Requested interval extends beyond decoded video")
            if decoder.wait(timeout=30) != 0:
                decode_error.seek(0)
                raise RuntimeError(decode_error.read(4096).decode("utf-8", "replace"))
            os.replace(partial, args.output)
        finally:
            for process in (decoder, probe):
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
                if process.stdout is not None:
                    process.stdout.close()
            if partial.exists():
                partial.unlink()
    metadata = {"contract": "svai-source-pixel-replay-conversion-v1", "source_file": args.video.name,
                "source_sha256": digest(args.video), "replay_sha256": digest(args.output), "width": width,
                "height": height, "source_start_frame": args.start_frame, "frames": count,
                "first_original_pts_100ns": first, "last_original_pts_100ns": last,
                "timeline": "Replay video time zero equals the first selected original PTS",
                "labels_created": False, "pixels_resized": False, "frame_rate_converted": False}
    args.output.with_suffix(".source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metadata))


if __name__ == "__main__":
    main()
