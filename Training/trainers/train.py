"""Train one baseline on reviewed, permission-cleared source-grouped video samples."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import random
import re
import subprocess
import time

import numpy as np
import torch
from torch.utils.data import DataLoader

from Training.datasets.splits import read_jsonl, load_splits, validate_splits
from Training.datasets.video_samples import VideoSamples, STATES, CLASSES, DIRECTIONS, DEFAULT_ROI
from Training.losses import multitask_loss
from Training.metrics.quality import summarize_predictions, choose_threshold
from Training.models import build_model
from Training.models.temporal import ARCHITECTURES
from Evaluation.false_positive.mine import review_queue


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False)+"\n", encoding="utf-8")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git_commit():
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def code_provenance():
    root = Path(__file__).resolve().parents[2]
    digest = hashlib.sha256()
    for path in sorted([*root.joinpath("Training").rglob("*.py"), *root.joinpath("Evaluation").rglob("*.py")]):
        digest.update(path.relative_to(root).as_posix().encode()+b"\0"+path.read_bytes()+b"\0")
    try:
        dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=normal"], cwd=root, text=True).strip())
    except (OSError, subprocess.CalledProcessError):
        dirty = None
    return {"git_commit": git_commit(), "working_tree_dirty": dirty, "training_evaluation_code_sha256": digest.hexdigest()}


def seed_everything(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)


def move_targets(values, device):
    return {key: value.to(device) for key, value in values.items()}


@torch.no_grad()
def evaluate(model, dataset, device, batch_size=1):
    model.eval()
    predictions = []
    for frames, _, _, indices in DataLoader(dataset, batch_size=batch_size, shuffle=False, num_workers=0):
        outputs = [value.cpu().numpy() for value in model(frames.to(device))]
        for offset, index in enumerate(indices.tolist()):
            source = dataset.rows[index]
            predictions.append({"clip_id": source["clip_id"], "annotation_id": source.get("annotation_id"),
                                "source_id": source["source_id"], "boss": source.get("boss", "UNKNOWN"),
                                "source_pts_ms": source.get("source_pts_ms"), "labels": source.get("labels", {}),
                                "attack_probability": float(outputs[0][offset, 0]),
                                "threat_probability": float(outputs[1][offset, 0]),
                                "tti_ms": float(outputs[2][offset, 0]),
                                "tti_uncertainty_ms": float(outputs[3][offset, 0]),
                                "predicted_state": STATES[int(outputs[4][offset].argmax())],
                                "predicted_class": CLASSES[int(outputs[5][offset].argmax())],
                                "observed_direction_prediction": DIRECTIONS[int(outputs[6][offset].argmax())]})
    return predictions


def calibration_temperature(rows, head):
    usable = [r for r in rows if r["labels"].get(head) is not None]
    if not usable or len({r["labels"][head] for r in usable}) < 2:
        return 1.0, "not_fitted_missing_validation_classes"
    y = np.asarray([r["labels"][head] for r in usable], dtype=np.float64)
    p = np.clip([r[f"{head}_probability"] for r in usable], 1e-6, 1-1e-6)
    logits = np.log(p/(1-p))
    candidates = np.exp(np.linspace(np.log(0.5), np.log(4.0), 64))
    losses = [np.mean(np.logaddexp(0, logits/temp)-y*logits/temp) for temp in candidates]
    return float(candidates[int(np.argmin(losses))]), "temperature_fitted_on_validation_only"


def label_support(rows):
    def binary(head):
        values = [r.get("labels", {}).get(head) for r in rows]
        return {"positive": sum(v is True for v in values), "negative": sum(v is False for v in values)}
    timing = sum(r.get("labels", {}).get("tti_ms") is not None and not r["labels"].get("tti_censored", True)
                 and r["labels"].get("impact_evidence") == "OBSERVED_CONTACT" for r in rows)
    return {"attack": binary("attack"), "threat": binary("threat"), "observed_tti": timing}


def train(args):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", args.model_version):
        raise ValueError("Model version must be a simple version name, not a path")
    seed_everything(args.seed)
    torch.set_num_threads(args.cpu_threads)
    rows = read_jsonl(args.manifest)
    if args.pretrained:
        if not args.pretrained.is_file():
            raise FileNotFoundError(args.pretrained)
    splits = load_splits(args.splits)
    validate_splits(rows, splits)
    if not splits.get("train"):
        raise ValueError("No training source IDs; run Training.datasets.splits on acquired reviewed samples")
    if not rows or any(r.get("example_only", False) for r in rows):
        raise ValueError("Gameplay train command refuses empty or synthetic samples; use trainers.smoke for synthetic tests")
    dataset_options = dict(manifest=args.manifest, frames=args.frames, size=args.size)
    datasets = {name: VideoSamples(sources=sources, augment=name == "train", **dataset_options)
                for name, sources in splits.items()}
    training = datasets["train"]
    if not len(training):
        raise ValueError("No samples match train_sources.txt")
    args.output.mkdir(parents=True, exist_ok=False)
    device = torch.device(args.device)
    model = build_model(args.architecture, args.frames, args.width).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    if args.pretrained:
        initial = torch.load(args.pretrained, map_location="cpu", weights_only=True)
        model.load_state_dict(initial["model"], strict=True)
    loader = DataLoader(training, batch_size=args.batch_size, shuffle=True, num_workers=0)
    history, started = [], time.perf_counter()
    for epoch in range(args.epochs):
        model.train()
        losses = []
        for frames, labels, masks, _ in loader:
            optimizer.zero_grad(set_to_none=True)
            loss, components = multitask_loss(model(frames.to(device)), move_targets(labels, device), move_targets(masks, device))
            if not torch.isfinite(loss):
                raise RuntimeError("Non-finite training loss")
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5)
            optimizer.step()
            losses.append(float(loss.detach()))
        summary = {"epoch": epoch+1, "mean_train_loss": float(np.mean(losses)), "batches": len(losses)}
        history.append(summary)
        print(json.dumps(summary), flush=True)
    validation = datasets.get("val")
    validation_rows = evaluate(model, validation, device) if validation is not None and len(validation) else []
    calibration = {}
    for head in ("attack", "threat"):
        temperature, status = calibration_temperature(validation_rows, head)
        getattr(model.heads, f"{head}_temperature").fill_(temperature)
        calibration[head] = {"temperature": temperature, "status": status}
    validation_rows = evaluate(model, validation, device) if validation is not None and len(validation) else []
    thresholds, threshold_notes = {}, {}
    for head in ("attack", "threat"):
        thresholds[head], threshold_notes[head] = choose_threshold(validation_rows, head)
    support = label_support(training.rows)
    config = {"contract": "temporal-v1", "model_version": args.model_version, "architecture": args.architecture,
              "frames": args.frames, "size": args.size, "width": args.width, "batch": 1,
              "input_name": "frames", "layout": "NTCHW", "preprocess": "roi-rgb-bilinear-v1",
              "roi": DEFAULT_ROI, "sample_interval_ms": 1000/30, "seed": args.seed,
              "training_status": "trained", "trained_samples": len(training),
              "threat_supported": support["threat"]["positive"] > 0 and support["threat"]["negative"] > 0,
              "tti_supported": support["observed_tti"] > 0,
              "attack_supported": support["attack"]["positive"] > 0 and support["attack"]["negative"] > 0,
              "states": STATES, "classes": CLASSES, "directions": DIRECTIONS,
              "direction_semantics": "observed screen action, not a safe action recommendation",
              "tti_semantics": "Laplace location/scale for observed contact; no-hit point labels masked",
              "thresholds": thresholds, "calibration": calibration, "threshold_selection": threshold_notes,
              "annotation_support": support, "supervised_epochs": args.epochs,
              "manifest_sha256": sha256(args.manifest)}
    config["code_provenance"] = code_provenance()
    if args.pretrained:
        config["pretrained_checkpoint_sha256"] = sha256(args.pretrained)
        provenance = args.pretrained.parent/"sources.json"
        config["pretraining_source_provenance"] = json.loads(provenance.read_text(encoding="utf-8")) if provenance.exists() else {"status": "unavailable"}
        # Record exposure instead of presenting a pretraining-exposed boss/source
        # as unseen. The split remains frozen and training may still proceed.
        seen = set(config["pretraining_source_provenance"].get("source_ids", []))
        config["pretraining_holdout_overlap"] = {name: sorted(seen & sources) for name, sources in splits.items() if name != "train"}
    config["auto_eligible"] = config["attack_supported"] and config["threat_supported"] and config["tti_supported"]
    metrics = {"evidence": "reviewed gameplay sample training; no live Sekiro test in this command",
               "training_seconds": time.perf_counter()-started, "history": history,
               "environment": {"platform": platform.platform(), "torch": str(torch.__version__),
                               "device": str(device), "cpu_threads": args.cpu_threads},
               "validation": summarize_predictions(validation_rows, thresholds)}
    # Training/validation review queues can feed the next iteration. Held-out
    # test sources stay frozen; mining them into the next train set would require
    # explicitly retiring that test and creating a new independent holdout.
    train_predictions = evaluate(model, VideoSamples(sources=splits["train"], **dataset_options), device)
    hard_examples = review_queue(train_predictions+validation_rows, thresholds["threat"])
    (args.output/"hard_negative_review_queue.jsonl").write_text("".join(json.dumps(row)+"\n" for row in hard_examples), encoding="utf-8")
    metrics["hard_negative_review_candidates"] = len(hard_examples)
    for split in ("test", "heldout_boss"):
        data = datasets.get(split)
        predictions = evaluate(model, data, device) if data is not None and len(data) else []
        metrics[split] = summarize_predictions(predictions, thresholds)
        (args.output/f"{split}_predictions.jsonl").write_text("".join(json.dumps(row)+"\n" for row in predictions), encoding="utf-8")
    torch.save({"model": model.cpu().state_dict(), "config": config}, args.output/"checkpoint.pt")
    write_json(args.output/"config.json", config)
    write_json(args.output/"metrics.json", metrics)
    write_json(args.output/"sources.json", {"manifest_sha256": sha256(args.manifest),
               "splits": {name: sorted(sources) for name, sources in splits.items()},
               "bosses": sorted({row.get("boss", "UNKNOWN") for row in training.rows}),
               "source_counts": {name: len(sources) for name, sources in splits.items()}})
    (args.output/"git_commit.txt").write_text(git_commit()+"\n", encoding="utf-8")
    print(json.dumps({"bundle": str(args.output), "training_status": "trained", "auto_eligible": config["auto_eligible"]}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("splits", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--model-version", required=True)
    parser.add_argument("--architecture", choices=ARCHITECTURES, default="cnn_gru")
    parser.add_argument("--frames", type=int, choices=(8, 16, 24, 32, 48), default=16)
    parser.add_argument("--size", type=int, choices=(320, 384, 416, 512), default=320)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--epochs", type=int, default=5)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--lr", type=float, default=0.001)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--cpu-threads", type=int, default=2)
    parser.add_argument("--pretrained", type=Path)
    args = parser.parse_args()
    if args.epochs < 1 or args.batch_size < 1 or args.lr <= 0 or args.cpu_threads < 1:
        parser.error("epochs, batch-size, learning rate and cpu-threads must be positive")
    train(args)


if __name__ == "__main__":
    main()
