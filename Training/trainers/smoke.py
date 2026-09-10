"""Actual synthetic optimization/export tests. Contains NO Sekiro training data."""
import argparse
import json
from pathlib import Path
import platform
import time

import torch

from Training.models import build_model
from Training.models.temporal import ARCHITECTURES
from Training.losses import multitask_loss
from Training.export.onnx_bundle import export_bundle
from Training.trainers.train import write_json, git_commit, seed_everything, code_provenance
from Evaluation.latency.benchmark import benchmark_model


def synthetic_batch(count=8, frames=16, size=64):
    """Moving colored tiles with mathematical labels, never gameplay labels."""
    x = torch.full((count, frames, 3, size, size), 0.02)
    labels = {"attack": torch.zeros(count), "threat": torch.zeros(count),
              "tti": torch.zeros(count), "state": torch.zeros(count, dtype=torch.long),
              "class": torch.zeros(count, dtype=torch.long), "direction": torch.zeros(count, dtype=torch.long),
              "attack_direction": torch.full((count,), 7, dtype=torch.long)}
    for sample in range(count):
        positive = sample % 2 == 1
        labels["attack"][sample] = positive
        labels["threat"][sample] = positive
        labels["tti"][sample] = 120 if positive else 500
        labels["state"][sample] = 4 if positive else 0
        labels["class"][sample] = 0 if positive else 13
        labels["direction"][sample] = 1 if positive else 4
        labels["attack_direction"][sample] = 0 if positive else 7
        for frame in range(frames):
            left = int((size//4 + frame*size/(frames*3)) if positive else size//4)
            x[sample, frame, 0 if positive else 2, size//4:size//2, left:left+size//5] = 0.9
            x[sample, frame] += 0.01*sample
    masks = {name: torch.ones(count) for name in labels}
    return x.clamp_(0, 1), labels, masks


def run(output, steps=80, frames=16, size=320):
    torch.set_num_threads(2)
    output.mkdir(parents=True, exist_ok=False)
    report = {"evidence": "SYNTHETIC ONLY: optimizer, architecture, ONNX and CPU execution checks",
              "gameplay_sources": 0, "reviewed_gameplay_attack_labels": 0, "trained_gameplay_models": 0,
              "platform": platform.platform(), "torch": str(torch.__version__), "models": {}}
    report["code_provenance"] = code_provenance()
    for architecture in ARCHITECTURES:
        seed_everything(23)
        started = time.perf_counter()
        model = build_model(architecture, frames, 32)
        optimizer = torch.optim.AdamW(model.parameters(), lr=0.003)
        data, labels, masks = synthetic_batch(frames=frames)
        with torch.no_grad():
            before = float(multitask_loss(model(data), labels, masks)[0])
        for _ in range(steps):
            optimizer.zero_grad(set_to_none=True)
            loss, _ = multitask_loss(model(data), labels, masks)
            if not torch.isfinite(loss):
                raise RuntimeError(f"Non-finite synthetic loss for {architecture}")
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5)
            optimizer.step()
        with torch.no_grad():
            after = float(multitask_loss(model.eval()(data), labels, masks)[0])
        if after >= before:
            raise RuntimeError(f"Synthetic optimizer did not reduce loss for {architecture}: {before} -> {after}")
        bundle = output/f"synthetic-only-{architecture}"
        bundle.mkdir()
        config = {"contract": "temporal-v2", "model_version": f"synthetic-smoke-{architecture}",
                  "architecture": architecture, "frames": frames, "size": size, "width": 32,
                  "training_status": "synthetic_smoke", "trained_samples": 0, "supervised_epochs": 0,
                  "attack_supported": False, "threat_supported": False, "tti_supported": False,
                  "attack_direction_supported": False,
                  "auto_eligible": False, "sample_interval_ms": 1000/30,
                  "preprocess": "roi-rgb-bilinear-v1", "seed": 23,
                  "fixture_training_size": 64, "deployment_parity_size": size}
        config["code_provenance"] = report["code_provenance"]
        training_metrics = {"evidence": "synthetic moving tiles; no boss labels or gameplay accuracy",
                            "steps": steps, "training_batch_shape": list(data.shape),
                            "initial_loss": before, "final_loss": after,
                            "optimization_seconds": time.perf_counter()-started,
                            "parameters": sum(p.numel() for p in model.parameters())}
        torch.save({"model": model.state_dict(), "config": config}, bundle/"checkpoint.pt")
        write_json(bundle/"config.json", config)
        write_json(bundle/"metrics.json", training_metrics)
        write_json(bundle/"sources.json", {"type": "generated_colored_tiles", "gameplay_sources": [], "seed": 23})
        (bundle/"git_commit.txt").write_text(git_commit()+"\n", encoding="utf-8")
        parity = export_bundle(bundle)
        latency = benchmark_model(bundle/"model.onnx", iterations=30, warmup=5)
        write_json(bundle/"cpu_latency.json", latency)
        report["models"][architecture] = {"optimization": training_metrics, "parity": parity,
                                         "cpu_latency": {key: value for key, value in latency.items() if key != "raw_ms"}}
        print(json.dumps({"architecture": architecture, "initial_loss": before, "final_loss": after,
                          "cpu_p50_ms": latency["p50_ms"], "auto_eligible": False}), flush=True)
    write_json(output/"synthetic-evidence.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--steps", type=int, default=80)
    parser.add_argument("--frames", type=int, choices=(8, 16, 24, 32, 48), default=16)
    parser.add_argument("--size", type=int, choices=(320, 384, 416, 512), default=320)
    args = parser.parse_args()
    if args.steps < 1:
        parser.error("steps must be positive")
    run(args.output, args.steps, args.frames, args.size)


if __name__ == "__main__":
    main()
