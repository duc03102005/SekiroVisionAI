"""Compare actual C++ preprocessing + detection against Python on real frames."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

import cv2
import numpy as np
import onnxruntime as ort
import torch
from torch.nn import functional as F

from Training.targets.train import decode, digest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-test", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--sources", default="data/source_manifest.jsonl")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output); output.mkdir(parents=True, exist_ok=True)
    sources = {row["source_id"]: row for row in map(json.loads, Path(args.sources).read_text().splitlines())}
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    session = ort.InferenceSession(args.model, sess_options=options, providers=["CPUExecutionProvider"])
    reports = []
    torch.set_num_threads(2)
    for suffix, index in [("005", 33), ("005", 99), ("026", 175)]:
        source = sources["LIVE_YT_GAMING_SEKIRO_"+suffix]
        decoder = cv2.VideoCapture(source["media_path"])
        try:
            bgr = None
            for _ in range(index+1):
                ok, bgr = decoder.read()
                if not ok:
                    raise ValueError("Actual source frame unavailable")
        finally:
            decoder.release()
        rgb = torch.from_numpy(np.ascontiguousarray(bgr[:, :, ::-1])).permute(2, 0, 1).float()/255
        reference = F.interpolate(rgb[None], size=(192, 320), mode="bilinear", align_corners=False).numpy()
        bgra = cv2.cvtColor(bgr, cv2.COLOR_BGR2BGRA)
        frame_path = output/f"{suffix}-{index}.frame"
        frame_path.write_bytes(struct.pack("<iii", bgr.shape[1], bgr.shape[0], bgr.shape[1]*4)+bgra.tobytes())
        result_path = output/f"{suffix}-{index}.json"
        subprocess.run([args.native_test, args.model, str(frame_path), str(result_path)], check=True)
        native_input = np.fromfile(str(result_path)+".rgb-f32", dtype="<f4").reshape(reference.shape)
        np.testing.assert_allclose(native_input, reference, atol=1e-6, rtol=1e-5)
        predicted = session.run(None, {"frame": reference})
        expected = decode(predicted[0][0], predicted[1][0])
        actual = json.loads(result_path.read_text())
        if len(actual) != len(expected):
            raise AssertionError("Python and native NMS detection counts differ")
        for a, b in zip(actual, expected):
            if a["role"] != b["role"]:
                raise AssertionError("Python and native role outputs differ")
            np.testing.assert_allclose(a["box"], b["box"], atol=2e-5, rtol=2e-4)
            np.testing.assert_allclose(a["confidence"], b["confidence"], atol=2e-5, rtol=2e-4)
        reports.append({"source_id": source["source_id"], "source_frame": index,
                        "source_sha256": source["sha256"], "detections": len(actual),
                        "preprocessing_max_abs": float(np.max(np.abs(native_input-reference))),
                        "native_python_detections_match": True})
    report = {"model_sha256": digest(args.model), "actual_gameplay_native_parity": reports,
              "meaning": "Software parity only; not held-out detection accuracy or Dodge success."}
    (output/"native-parity.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
