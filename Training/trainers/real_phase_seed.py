"""Reproduce the real-video motion pretraining and reviewed phase experiment.

This is a development/CI training job, not a production model selection. Its
fixed reviewed cores do not contain exact contact/TTI or attack-direction labels.
The resulting phase model must keep threat/TTI/direction/Auto unsupported.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from DatasetTools.common import load_jsonl, write_jsonl
from Training.datasets.prepare_unlabeled import prepare
from Training.datasets.reviewed_intervals import export as export_intervals
from Training.export.onnx_bundle import export_bundle
from Training.trainers.train import sha256, write_json, code_provenance

LIVE_IDS = tuple(f"LIVE_YT_GAMING_SEKIRO_{suffix}" for suffix in ("005", "016", "019", "023", "026", "029"))


def run(sources, output, pretrain_epochs=2, phase_epochs=3):
    root = Path(__file__).resolve().parents[2]
    sources, output = Path(sources).resolve(), Path(output).resolve()
    imported = {row["source_id"]: row for row in load_jsonl(sources)}
    missing = sorted(set(LIVE_IDS)-set(imported))
    if missing:
        raise ValueError(f"Acquire all six pinned LIVE-YT sources first: {missing}")
    if pretrain_epochs < 1 or phase_epochs < 1:
        raise ValueError("Learning epochs must be positive")
    output.mkdir(parents=True, exist_ok=False)
    starting_code = code_provenance()
    selected = output/"selected-sources.jsonl"
    write_jsonl(selected, [imported[source] for source in LIVE_IDS])
    windows = output/"prepared"
    preparation = prepare(selected, windows, frames=16, stride=8, max_width=1280)
    representation = output/"motion-representation"
    phase = output/"phase-model"
    reviews = root/"Training/configs/combat-review-intervals-v1.jsonl"
    samples = output/"reviewed-phase-samples.jsonl"
    reviewed = export_intervals(windows/"clips.jsonl", reviews, samples, frames=16)
    splits = output/"splits"
    commands = [
        [sys.executable, "-m", "Training.trainers.pretrain", str(windows/"unlabeled_samples.jsonl"),
         str(representation), "--architecture", "optical_flow_fusion", "--frames", "16", "--size", "320",
         "--epochs", str(pretrain_epochs), "--batch-size", "4", "--device", "cpu", "--temporal-weight", "0"],
        [sys.executable, "-m", "Training.datasets.splits", str(samples), str(splits)],
        [sys.executable, "-m", "Training.trainers.train", str(samples), str(splits), str(phase),
         "--model-version", "hirata-phase-dev-v1", "--architecture", "optical_flow_fusion",
         "--pretrained", str(representation/"checkpoint.pt"), "--epochs", str(phase_epochs),
         "--frames", "16", "--size", "320", "--batch-size", "4", "--cpu-threads", "2", "--device", "cpu"],
    ]
    write_json(output/"commands.json", {"cwd": str(root), "argv": commands, "code_provenance": starting_code})
    for command in commands:
        subprocess.run(command, cwd=root, check=True)
    parity = {"representation": export_bundle(representation), "phase": export_bundle(phase)}
    config = json.loads((phase/"config.json").read_text(encoding="utf-8"))
    if not config["attack_supported"] or not config["state_supported"] or any(config[key] for key in (
            "threat_supported", "tti_supported", "attack_direction_supported", "class_supported", "observed_direction_supported", "auto_eligible")):
        raise RuntimeError("The fixed phase experiment produced incorrect trained-head support flags")
    evidence = {
        "evidence": "ACTUAL VIDEO, AI VISUAL PHASE REVIEW; NOT A FUNCTIONAL AUTO-DODGE RELEASE",
        "source_sha256": {source: imported[source]["sha256"] for source in LIVE_IDS},
        "source_groups": sorted({imported[source]["source_group_id"] for source in LIVE_IDS}),
        "review_spec_sha256": sha256(reviews), "preparation": preparation, "reviewed_samples": reviewed,
        "code_provenance": starting_code, "pretrain_epochs": pretrain_epochs, "phase_epochs": phase_epochs,
        "representation_checkpoint_sha256": sha256(representation/"checkpoint.pt"),
        "phase_checkpoint_sha256": sha256(phase/"checkpoint.pt"),
        "phase_model_sha256": sha256(phase/"model.onnx"), "phase_config": config, "parity": parity,
        "independent_validation_groups": 0, "observed_contact_samples": 0, "auto_dodge_eligible": False,
    }
    write_json(output/"run-evidence.json", evidence)
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--pretrain-epochs", type=int, default=2)
    parser.add_argument("--phase-epochs", type=int, default=3)
    args = parser.parse_args()
    result = run(args.sources, args.output, args.pretrain_epochs, args.phase_epochs)
    print(json.dumps({"phase_model_sha256": result["phase_model_sha256"], "auto_dodge_eligible": False}))


if __name__ == "__main__":
    main()
