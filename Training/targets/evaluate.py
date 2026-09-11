"""Frozen source-family holdout check; never silently mix it with fitting data."""
import argparse
import json
from pathlib import Path
import onnxruntime as ort
import torch
from Training.targets.train import digest, evaluate, load_data, provenance_keys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--training-report", required=True)
    parser.add_argument("--annotations", required=True)
    parser.add_argument("--sources", default="data/source_manifest.jsonl")
    parser.add_argument("--output", required=True)
    parser.add_argument("--partition", choices=["test", "val"], default="test")
    args = parser.parse_args()
    torch.set_num_threads(2)
    training = json.loads(Path(args.training_report).read_text())
    if training["onnx_sha256"] != digest(args.model):
        raise ValueError("Training evidence does not match frozen model hash")
    rows, images = load_data(args.annotations, args.sources)
    if {row["split"] for row in rows} != {args.partition}:
        raise ValueError("Evaluation records do not match declared partition")
    groups = sorted({row["source_group_id"] for row in rows})
    if set(groups).intersection(training["source_groups"]):
        raise ValueError("Evaluation source family overlaps training")
    sources = [json.loads(line) for line in Path(args.sources).read_text().splitlines() if line.strip()]
    train_identities = set().union(*(provenance_keys(source) for source in sources
                                    if source["source_group_id"] in training["source_groups"]))
    test_identities = set().union(*(provenance_keys(source) for source in sources
                                   if source["source_group_id"] in groups))
    if train_identities.intersection(test_identities):
        raise ValueError("Evaluation player/creator/session identity overlaps training")
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    session = ort.InferenceSession(args.model, sess_options=options, providers=["CPUExecutionProvider"])
    for _ in range(3):
        session.run(None, {"frame": images[:1].numpy()})
    metrics, predictions, latency = evaluate(session, rows, images)
    report = {"evaluation": "DEVELOPMENT_VALIDATION_NOT_FINAL_HOLDOUT" if args.partition=="val" else "SMALL_FROZEN_SOURCE_HOLDOUT_NOT_PRODUCTION_ACCEPTANCE",
              "model_sha256": digest(args.model), "annotation_sha256": digest(args.annotations),
              "frames": len(rows), "train_groups": training["source_groups"], "test_groups": groups,
              "group_overlap": False, "player_disjointness": "UNKNOWN_ORIGINAL_PLAYER_IDENTITIES",
              "annotation_review": "AI_VISUAL_REVIEW_NOT_INDEPENDENT_HUMAN_GOLD",
              "thresholds_frozen_from_training_run": training["thresholds"],
              "metrics": metrics, "cpu_model_ms_shared_development_host": latency,
              "limitations": "Seven reviewed frames from one mounted boss session do not establish all-boss generalization, temporal continuity or Dodge success."}
    output = Path(args.output); output.mkdir(parents=True, exist_ok=True)
    report_name = "development-validation.json" if args.partition=="val" else "source-holdout.json"
    (output/report_name).write_text(json.dumps(report, indent=2)+"\n")
    (output/"predictions.jsonl").write_text("".join(json.dumps(row)+"\n" for row in predictions))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
