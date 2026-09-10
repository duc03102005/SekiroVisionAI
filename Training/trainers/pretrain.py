"""Optional contrastive temporal pretraining on permission-cleared unlabeled clips.

This learns clip representations from photometric views and an optional
same-clip temporal-reversal objective. It is not VideoMAE, attack supervision,
pose pretraining, or evidence that a threat head is trained.
"""
import argparse
from pathlib import Path
import platform
import time

import torch
from torch.nn import functional as F
from torch.utils.data import DataLoader

from Training.datasets.video_samples import VideoSamples
from Training.models import build_model
from Training.models.temporal import ARCHITECTURES
from Training.trainers.train import write_json, seed_everything, git_commit, sha256, code_provenance


def contrastive_loss(model, frames, temperature=0.2, temporal_weight=0.25):
    view_a = (frames*0.9+0.03).clamp(0, 1)
    view_b = (frames*1.1-0.02).clamp(0, 1)
    a, b = F.normalize(model.encode(view_a), dim=-1), F.normalize(model.encode(view_b), dim=-1)
    labels = torch.arange(frames.shape[0], device=frames.device)
    similarity = a @ b.T / temperature
    result = (F.cross_entropy(similarity, labels)+F.cross_entropy(similarity.T, labels))/2
    if temporal_weight:
        # Reverse only this already observed history as a pretext negative.
        # Same scene/character appearance cannot solve forward-versus-reverse.
        # Static clips are excluded because their arrow of time is unobservable.
        reverse = F.normalize(model.encode(view_b.flip(1)), dim=-1)
        motion = (frames[:, 1:]-frames[:, :-1]).abs().mean((1, 2, 3, 4))
        mask = (motion > 0.005).to(a.dtype)
        order_loss = F.relu(0.15 + (a*reverse).sum(-1) - (a*b).sum(-1))
        result = result + temporal_weight*(order_loss*mask).sum()/mask.sum().clamp_min(1)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--architecture", choices=ARCHITECTURES, default="cnn_gru")
    parser.add_argument("--frames", type=int, choices=(8, 16, 24, 32, 48), default=16)
    parser.add_argument("--size", type=int, choices=(320, 384, 416, 512), default=320)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--temporal-weight", type=float, default=0.25)
    args = parser.parse_args()
    if args.batch_size < 2 or args.epochs < 1 or not 0 <= args.temporal_weight <= 2:
        parser.error("Need batch-size>=2, epochs>=1 and temporal-weight in [0,2]")
    seed_everything(41)
    torch.set_num_threads(2)
    data = VideoSamples(args.manifest, frames=args.frames, size=args.size, augment=True, require_review=False)
    if len(data) < args.batch_size:
        raise ValueError("Need at least one full batch of acquired unlabeled clips")
    args.output.mkdir(parents=True, exist_ok=False)
    model = build_model(args.architecture, args.frames, 64).to(args.device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=0.0003)
    losses = []
    started = time.perf_counter()
    for _ in range(args.epochs):
        for frames, _, _, _ in DataLoader(data, batch_size=args.batch_size, shuffle=True, drop_last=True):
            optimizer.zero_grad(set_to_none=True)
            loss = contrastive_loss(model, frames.to(args.device), temporal_weight=args.temporal_weight)
            loss.backward()
            optimizer.step()
            losses.append(float(loss.detach()))
    config = {"contract": "temporal-v2", "architecture": args.architecture, "frames": args.frames, "size": args.size, "width": 64,
              "model_version": args.output.name, "training_status": "self_supervised", "auto_eligible": False,
              "attack_supported": False, "threat_supported": False, "tti_supported": False,
              "attack_direction_supported": False,
              "trained_samples": 0, "supervised_epochs": 0,
              "pretraining_temporal_weight": args.temporal_weight}
    config["code_provenance"] = code_provenance()
    torch.save({"model": model.cpu().state_dict(), "config": config}, args.output/"checkpoint.pt")
    write_json(args.output/"config.json", config)
    write_json(args.output/"metrics.json", {"method": "symmetric photometric clip contrastive + masked same-clip temporal reversal", "losses": losses,
               "optimization_steps": len(losses), "training_seconds": time.perf_counter()-started,
               "epochs": args.epochs, "batch_size": args.batch_size, "sample_count": len(data),
               "environment": {"platform": platform.platform(), "torch": str(torch.__version__), "device": args.device},
               "gameplay_attack_metrics": None, "compare_against_supervised_only_with_frozen_splits": True})
    write_json(args.output/"sources.json", {"source_ids": sorted({r["source_id"] for r in data.rows}),
               "manifest_sha256": sha256(args.manifest),
               "source_groups": sorted({r.get("source_group_id", r["source_id"]) for r in data.rows}),
               "source_provenance": {r["source_id"]: {key: r.get(key) for key in
                    ("source_sha256", "source_url", "source_group_id", "player_id", "session_id", "usage_basis", "usage_evidence")}
                    for r in data.rows},
               "holdout_claim": "Pretraining exposure only; no attack labels or independent generalization test"})
    (args.output/"git_commit.txt").write_text(git_commit()+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
