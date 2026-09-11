"""One frozen-checkpoint evaluation on reviewed, source-selected video anchors.

This is not the native ReplayHarness and does not establish Dodge success. It
measures model generalization without changing thresholds or optimizer weights.
"""
import argparse
import json
from pathlib import Path

import torch

from Training.datasets.video_samples import VideoSamples
from Training.models import build_model
from Training.metrics.quality import summarize_predictions
from Training.trainers.train import evaluate, sha256, write_json, code_provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("samples", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-id", action="append", required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError("Evaluation reports are immutable")
    torch.set_num_threads(2)
    checkpoint = torch.load(args.bundle/"checkpoint.pt", map_location="cpu", weights_only=True)
    config = checkpoint["config"]
    model = build_model(config["architecture"], config["frames"], config["width"], contract=config["contract"])
    model.load_state_dict(checkpoint["model"])
    dataset = VideoSamples(args.samples, sources=set(args.source_id), frames=config["frames"], size=config["size"])
    if not len(dataset):
        raise ValueError("No reviewed samples for selected source IDs")
    predictions = evaluate(model, dataset, "cpu")
    for row in predictions:
        if not config.get("class_supported"):
            row["predicted_class"] = "UNKNOWN_ATTACK"
        if not config.get("state_supported"):
            row["predicted_state"] = "UNKNOWN_STATE"
        if not config.get("attack_direction_supported"):
            row["attack_direction_prediction"] = "UNKNOWN"
    seen = set(config.get("pretraining_source_provenance", {}).get("source_ids", []))
    sources = json.loads((args.bundle/"sources.json").read_text(encoding="utf-8"))
    trained = set(sources.get("splits", {}).get("train", []))
    report = {
        "evidence": "AI-reviewed model anchor evaluation; not human gold or production Dodge acceptance",
        "checkpoint_sha256": sha256(args.bundle/"checkpoint.pt"), "samples_sha256": sha256(args.samples),
        "model_version": config["model_version"], "code_provenance": code_provenance(),
        "evaluated_sources": sorted(set(args.source_id)),
        "supervised_training_overlap": sorted(set(args.source_id)&trained),
        "pretraining_exposure_overlap": sorted(set(args.source_id)&seen),
        "review_methods": sorted({row.get("review_method", "unavailable") for row in dataset.rows}),
        "thresholds": config["thresholds"], "thresholds_changed_during_evaluation": False,
        "head_support": {key: config.get(key, False) for key in ("attack_supported", "threat_supported", "tti_supported",
                            "state_supported", "class_supported", "attack_direction_supported", "observed_direction_supported")},
        "metrics": summarize_predictions(predictions, config["thresholds"], supported_heads=config), "predictions": predictions,
    }
    write_json(args.output, report)
    print(json.dumps({"evaluated_sources": report["evaluated_sources"], "metrics": report["metrics"]}, indent=2))


if __name__ == "__main__":
    main()
