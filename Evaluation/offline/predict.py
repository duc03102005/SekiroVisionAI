"""Scan acquired clip windows with an ONNX model; output reviewable suggestions."""
import argparse
import json
from pathlib import Path

import onnxruntime as ort
from torch.utils.data import DataLoader

from Training.datasets.video_samples import VideoSamples
from Training.models import OUTPUT_NAMES
from Evaluation.false_positive.mine import review_queue


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--review-queue", type=Path)
    args = parser.parse_args()
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    session = ort.InferenceSession(str(args.model), sess_options=options, providers=["CPUExecutionProvider"])
    shape = session.get_inputs()[0].shape
    if len(shape) != 5 or shape[0] != 1 or shape[2] != 3 or shape[3] != shape[4]:
        raise ValueError("Expected temporal-v1 NTCHW RGB input")
    data = VideoSamples(args.manifest, frames=shape[1], size=shape[3], require_review=False)
    rows = []
    for frames, _, _, indices in DataLoader(data, batch_size=1, shuffle=False):
        source = data.rows[int(indices[0])]
        outputs = session.run(OUTPUT_NAMES, {"frames": frames.numpy()})
        rows.append({"source_id": source["source_id"], "clip_id": source["clip_id"],
                     "annotation_id": source.get("annotation_id"), "source_pts_ms": source.get("source_pts_ms"),
                     "labels": source.get("labels", {}), "boss": source.get("boss", "UNKNOWN"),
                     "model_metadata": session.get_modelmeta().custom_metadata_map,
                     "attack_probability": float(outputs[0][0, 0]), "threat_probability": float(outputs[1][0, 0]),
                     "tti_ms": float(outputs[2][0, 0]), "tti_uncertainty_ms": float(outputs[3][0, 0])})
    with args.output.open("x", encoding="utf-8") as stream:
        stream.writelines(json.dumps(row)+"\n" for row in rows)
    if args.review_queue:
        with args.review_queue.open("x", encoding="utf-8") as stream:
            stream.writelines(json.dumps(row)+"\n" for row in review_queue(rows))
    print(json.dumps({"windows_scanned": len(rows), "accepted_labels_written": 0}))


if __name__ == "__main__":
    main()
