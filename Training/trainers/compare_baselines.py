"""Train/export/measure causal RGB, TCN and flow models on one frozen dataset."""
import argparse
import json
from pathlib import Path
import re

from Training.models.temporal import ARCHITECTURES
from Training.trainers.train import train, write_json
from Training.export.onnx_bundle import export_bundle
from Evaluation.latency.benchmark import benchmark_model


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("splits", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--version-prefix", required=True)
    parser.add_argument("--epochs", type=int, default=5)
    parser.add_argument("--frames", type=int, choices=(8, 16, 24, 32, 48), default=16)
    parser.add_argument("--size", type=int, choices=(320, 384, 416, 512), default=320)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--lr", type=float, default=.001)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--provider", default="CPUExecutionProvider")
    parser.add_argument("--cpu-threads", type=int, default=2)
    args = parser.parse_args()
    if args.epochs < 1 or args.batch_size < 1 or args.lr <= 0 or args.cpu_threads < 1:
        parser.error("epochs, batch-size, lr and cpu-threads must be positive")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,100}", args.version_prefix):
        parser.error("version-prefix must be a simple version name, not a path")
    args.output.mkdir(parents=True, exist_ok=False)
    comparison = {"frozen_manifest": str(args.manifest), "frozen_splits": str(args.splits),
                  "architecture_winner": None, "live_false_dodge_metrics": None,
                  "selection_note": "Compare precision/false-positive denominators and measured runtime; no paper-based winner.",
                  "models": {}}
    for architecture in ARCHITECTURES:
        child = argparse.Namespace(**vars(args))
        child.output = args.output/f"{args.version_prefix}-{architecture}"
        child.architecture = architecture
        child.model_version = f"{args.version_prefix}-{architecture}"
        child.pretrained = None
        train(child)
        parity = export_bundle(child.output)
        latency = benchmark_model(child.output/"model.onnx", provider=args.provider, threads=args.cpu_threads)
        write_json(child.output/"measured-latency.json", latency)
        metrics = json.loads((child.output/"metrics.json").read_text(encoding="utf-8"))
        comparison["models"][architecture] = {"validation": metrics["validation"], "test": metrics["test"],
            "heldout_boss": metrics["heldout_boss"], "parity": parity,
            "latency": {key: value for key, value in latency.items() if key != "raw_ms"}}
        write_json(args.output/"comparison.json", comparison)
    print(json.dumps({"comparison": str(args.output/"comparison.json"), "models_compared": len(comparison["models"])}))


if __name__ == "__main__":
    main()
