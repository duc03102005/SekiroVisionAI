"""Build-only synthetic media; never gameplay, model training, or accuracy evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--ffmpeg")
    parser.add_argument("--svr-only", action="store_true")
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    width, height, frames, fps = 320, 192, 30, 30
    pixels = bytearray(width * height * 4)
    colors = [(0, 0, 255, 255), (0, 255, 0, 255), (255, 0, 0, 255), (255, 255, 255, 255)]
    for y in range(height):
        for x in range(width):
            quadrant = (2 if y >= height // 2 else 0) + (1 if x >= width // 2 else 0)
            offset = (y * width + x) * 4
            pixels[offset : offset + 4] = bytes(colors[quadrant])
    raw_path = args.directory / "replay-fixture.svr"
    with raw_path.open("wb") as stream:
        stream.write(b"SVRRAW01" + struct.pack("<II", width, height))
        for index in range(frames):
            stream.write(struct.pack("<qQQ", round(index * 10_000_000 / fps), 1, index + 1))
            stream.write(pixels)
    outputs = [raw_path]
    version = None
    if not args.svr_only:
        executable = args.ffmpeg or shutil.which("ffmpeg")
        if executable is None:
            import imageio_ffmpeg  # Optional pinned build-only wheel; never shipped to users.
            executable = imageio_ffmpeg.get_ffmpeg_exe()
        version = subprocess.run([executable, "-version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]
        mp4 = args.directory / "replay-fixture.mp4"
        command = [executable, "-nostdin", "-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "bgra",
                   "-video_size", f"{width}x{height}", "-framerate", str(fps), "-i", "pipe:0", "-an",
                   "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(mp4)]
        subprocess.run(command, input=bytes(pixels) * frames, check=True, timeout=60, capture_output=True)
        outputs.append(mp4)
    manifest = {"synthetic_only": True, "gameplay_frames": 0, "description": "Static RGB quadrants for native decoder orientation/color/PTS tests",
                "width": width, "height": height, "frames": frames, "fps": fps, "ffmpeg": version,
                "files": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in outputs}}
    (args.directory / "replay-fixture.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
