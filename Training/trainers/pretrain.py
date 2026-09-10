"""Optional contrastive temporal pretraining on permission-cleared unlabeled clips.

This learns clip representations from two photometric views. It is not VideoMAE,
attack supervision, pose pretraining, or evidence that a threat head is trained.
"""
import argparse
from pathlib import Path

import torch
from torch.nn import functional as F
from torch.utils.data import DataLoader

from Training.datasets.video_samples import VideoSamples
from Training.models import build_model
from Training.models.temporal import ARCHITECTURES
from Training.trainers.train import write_json, seed_everything, git_commit, sha256, code_provenance


def contrastive_loss(model, frames, temperature=0.2):
    view_a = (frames*0.9+0.03).clamp(0, 1)
    view_b = (frames*1.1-0.02).clamp(0, 1)
    a, b = F.normalize(model.encode(view_a), dim=-1), F.normalize(model.encode(view_b), dim=-1)
    labels = torch.arange(frames.shape[0], device=frames.device)
    similarity = a @ b.T / temperature
    return (F.cross_entropy(similarity, labels)+F.cross_entropy(similarity.T, labels))/2


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
    args = parser.parse_args()
    if args.batch_size < 2 or args.epochs < 1:
        parser.error("Contrastive learning needs batch-size>=2 and epochs>=1")
    seed_everything(41)
    torch.set_num_threads(2)
    data = VideoSamples(args.manifest, frames=args.frames, size=args.size, augment=True, require_review=False)
    if len(data) < args.batch_size:
        raise ValueError("Need at least one full batch of acquired unlabeled clips")
    args.output.mkdir(parents=True, exist_ok=False)
    model = build_model(args.architecture, args.frames, 64).to(args.device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=0.0003)
    losses = []
    for _ in range(args.epochs):
        for frames, _, _, _ in DataLoader(data, batch_size=args.batch_size, shuffle=True, drop_last=True):
            optimizer.zero_grad(set_to_none=True)
            loss = contrastive_loss(model, frames.to(args.device))
            loss.backward()
            optimizer.step()
            losses.append(float(loss.detach()))
    config = {"architecture": args.architecture, "frames": args.frames, "size": args.size, "width": 64,
              "model_version": args.output.name, "training_status": "self_supervised", "auto_eligible": False,
              "attack_supported": False, "threat_supported": False, "tti_supported": False,
              "trained_samples": 0, "supervised_epochs": 0}
    config["code_provenance"] = code_provenance()
    torch.save({"model": model.cpu().state_dict(), "config": config}, args.output/"checkpoint.pt")
    write_json(args.output/"config.json", config)
    write_json(args.output/"metrics.json", {"method": "symmetric clip contrastive photometric views", "losses": losses,
               "gameplay_attack_metrics": None, "compare_against_supervised_only_with_frozen_splits": True})
    write_json(args.output/"sources.json", {"source_ids": sorted({r["source_id"] for r in data.rows}),
               "manifest_sha256": sha256(args.manifest)})
    (args.output/"git_commit.txt").write_text(git_commit()+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
