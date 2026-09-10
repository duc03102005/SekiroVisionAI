"""Train/export an explicitly experimental actor detector from actual gameplay.

No random-frame split is created. The checked-in first review has one grouped
source family, so fitting metrics are NOT held-out accuracy or release evidence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import time

import cv2
import numpy as np
import onnx
import onnxruntime as ort
import torch
from torch.nn import functional as F

from Training.targets.model import RoleDetector, ROLES, WIDTH, HEIGHT, GRID_W, GRID_H


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def load_data(annotations, manifest):
    rows = [json.loads(line) for line in Path(annotations).read_text().splitlines() if line.strip()]
    sources = {row["source_id"]: row for row in map(json.loads, Path(manifest).read_text().splitlines())}
    found = {}
    for source_id in sorted({row["source_id"] for row in rows}):
        source = sources[source_id]
        if source.get("example_only") or source.get("usage_basis") not in {"OWN_RECORDING", "EXPLICIT_PERMISSION", "OPEN_LICENSE"}:
            raise ValueError("Only acquired, documented actual footage may supply target training")
        path = Path(source["media_path"])
        if digest(path) != source["sha256"]:
            raise ValueError("Source media hash mismatch")
        pts = {item["source_frame"]: item["source_pts_ms"] for item in map(json.loads, Path(source["pts_path"]).read_text().splitlines())}
        selected = {row["source_frame"]: row for row in rows if row["source_id"] == source_id}
        for index, row in selected.items():
            if row["source_sha256"] != source["sha256"] or abs(pts[index]-row["source_pts_ms"]) > 0.001:
                raise ValueError("Annotation source hash/PTS mismatch")
            if row["source_group_id"] != source["source_group_id"]:
                raise ValueError("Annotation source grouping does not match provenance")
        decoder = cv2.VideoCapture(str(path))
        try:
            index = 0
            while selected:
                ok, bgr = decoder.read()
                if not ok:
                    break
                if index in selected:
                    row = selected.pop(index)
                    if bgr.shape[:2] != (row["source_height"], row["source_width"]):
                        raise ValueError("Unexpected decoded source resolution")
                    rgb = torch.from_numpy(np.ascontiguousarray(bgr[:, :, ::-1])).permute(2, 0, 1).float()/255
                    rgb = F.interpolate(rgb[None], size=(HEIGHT, WIDTH), mode="bilinear", align_corners=False)[0]
                    found[row["annotation_id"]] = rgb
                index += 1
            if selected:
                raise ValueError("Annotated original source frame was not decoded")
        finally:
            decoder.release()
    group_splits = {}
    for row in rows:
        group_splits.setdefault(row["source_group_id"], set()).add(row["split"])
        if row.get("schema_version") != "target-roles-v1" or row.get("review_method") != "AI_VISUAL_REVIEW":
            raise ValueError("Unsupported or unreviewed target annotations")
        for obj in row["objects"]:
            if obj["role"] not in ROLES or len(obj["box"]) != 4:
                raise ValueError("Invalid target role or box")
            left, top, right, bottom = obj["box"]
            if not all(math.isfinite(v) for v in obj["box"]) or not 0 <= left < right <= 1 or not 0 <= top < bottom <= 1:
                raise ValueError("Invalid normalized box")
    if any(len(splits) != 1 for splits in group_splits.values()):
        raise ValueError("Source family leakage across target train/validation/test")
    useful = [row for row in rows if any(row["role_reviewed"].values())]
    return useful, torch.stack([found[row["annotation_id"]] for row in useful])


def transformed_batch(images, rows, indices, augment=True):
    batch = images[indices].clone()
    objects = [[dict(obj, box=list(obj["box"])) for obj in rows[index]["objects"]] for index in indices]
    if augment:
        # Whole-image geometry is transformed with all role boxes; augmented
        # examples retain their original source group and do not inflate counts.
        for i in range(len(indices)):
            if random.random() < 0.5:
                batch[i] = batch[i].flip(-1)
                for obj in objects[i]:
                    l, t, r, b = obj["box"]; obj["box"] = [1-r, t, 1-l, b]
        scale = torch.empty(len(indices)).uniform_(0.87, 1.08)
        tx = torch.empty(len(indices)).uniform_(-0.10, 0.10)
        ty = torch.empty(len(indices)).uniform_(-0.07, 0.07)
        theta = torch.zeros(len(indices), 2, 3)
        theta[:, 0, 0] = theta[:, 1, 1] = 1/scale
        theta[:, 0, 2] = -2*tx/scale; theta[:, 1, 2] = -2*ty/scale
        grid = F.affine_grid(theta, batch.shape, align_corners=False)
        batch = F.grid_sample(batch, grid, align_corners=False, padding_mode="zeros")
        for i, items in enumerate(objects):
            for obj in items:
                box = obj["box"]
                obj["box"] = [max(0., min(1., (v-0.5)*float(scale[i])+0.5+float(tx[i] if j%2==0 else ty[i]))) for j, v in enumerate(box)]
        batch = (batch*torch.empty(len(indices), 1, 1, 1).uniform_(0.75, 1.22) +
                 torch.empty(len(indices), 1, 1, 1).uniform_(-0.045, 0.045)).clamp(0, 1)
    heat = torch.zeros(len(indices), 2, GRID_H, GRID_W)
    boxes = torch.zeros(len(indices), 2, 4, GRID_H, GRID_W)
    positive = torch.zeros_like(heat)
    reviewed = torch.tensor([[rows[i]["role_reviewed"][role] for role in ROLES] for i in indices], dtype=torch.float32)[:, :, None, None]
    yy, xx = torch.meshgrid(torch.arange(GRID_H), torch.arange(GRID_W), indexing="ij")
    for i, items in enumerate(objects):
        for obj in items:
            role = ROLES.index(obj["role"])
            if not reviewed[i, role, 0, 0]:
                continue
            left, top, right, bottom = obj["box"]
            if right-left < 0.02 or bottom-top < 0.02:
                continue
            cx, cy = (left+right)/2*GRID_W, (top+bottom)/2*GRID_H
            gx, gy = min(GRID_W-1, int(cx)), min(GRID_H-1, int(cy))
            radius = max(0.65, min(1.5, (right-left)*GRID_W*0.15, (bottom-top)*GRID_H*0.15))
            gaussian = torch.exp(-((xx-gx).float().square()+(yy-gy).float().square())/(2*radius*radius))
            heat[i, role] = torch.maximum(heat[i, role], gaussian)
            positive[i, role, gy, gx] = 1
            boxes[i, role, :, gy, gx] = torch.tensor([cx-gx, cy-gy, right-left, bottom-top])
    return batch, heat, boxes, positive, reviewed


def loss_value(raw, heat, boxes, positive, reviewed):
    probability = raw[:, :, 0].sigmoid().clamp(1e-5, 1-1e-5)
    count = positive.sum().clamp_min(1)
    pos_loss = -probability.log()*(1-probability).square()*positive
    neg_loss = -(1-probability).log()*probability.square()*(1-heat).pow(4)*(1-positive)
    heat_loss = ((pos_loss+neg_loss)*reviewed).sum()/count
    box_loss = (F.smooth_l1_loss(raw[:, :, 1:].sigmoid(), boxes, reduction="none")*positive[:, :, None]).sum()/count
    return heat_loss + 8*box_loss


def iou(a, b):
    intersection = max(0, min(a[2], b[2])-max(a[0], b[0]))*max(0, min(a[3], b[3])-max(a[1], b[1]))
    return intersection/max(1e-9, (a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-intersection)


def decode(scores, boxes, threshold=0.75):
    result = []
    for role in range(2):
        candidates = []
        for y, x in zip(*np.where(scores[role] >= threshold)):
            box = boxes[role, :, y, x].tolist()
            if box[2]-box[0] < 0.02 or box[3]-box[1] < 0.02:
                continue
            candidates.append({"role": ROLES[role], "confidence": float(scores[role, y, x]), "box": box})
        kept = []
        for item in sorted(candidates, key=lambda row: -row["confidence"])[:64]:
            if not any(iou(item["box"], old["box"]) > 0.4 for old in kept):
                kept.append(item)
            if len(kept) == 8:
                break
        result.extend(kept)
    return result


def evaluate(session, rows, images):
    metrics = {role: {"tp": 0, "fp": 0, "fn": 0, "matched_iou": []} for role in ROLES}
    predictions, timings = [], []
    for row, image in zip(rows, images):
        start = time.perf_counter()
        scores, boxes = session.run(None, {"frame": image[None].numpy()})
        timings.append((time.perf_counter()-start)*1000)
        detections = decode(scores[0], boxes[0])
        predictions.append({"annotation_id": row["annotation_id"], "split": row["split"], "detections": detections})
        for role in ROLES:
            if not row["role_reviewed"][role]:
                continue
            truth = [obj["box"] for obj in row["objects"] if obj["role"] == role]
            used = set()
            for detected in (d for d in detections if d["role"] == role):
                matches = [(iou(detected["box"], box), i) for i, box in enumerate(truth) if i not in used]
                score, index = max(matches, default=(0, -1))
                if score >= 0.5:
                    metrics[role]["tp"] += 1; metrics[role]["matched_iou"].append(score); used.add(index)
                else:
                    metrics[role]["fp"] += 1
            metrics[role]["fn"] += len(truth)-len(used)
    for value in metrics.values():
        value["precision"] = value["tp"]/max(1, value["tp"]+value["fp"])
        value["recall"] = value["tp"]/max(1, value["tp"]+value["fn"])
        value["mean_matched_iou"] = float(np.mean(value.pop("matched_iou"))) if value["tp"] else None
    return metrics, predictions, {"p50": float(np.median(timings)), "p95": float(np.percentile(timings, 95)), "max": max(timings)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--annotations", default="Dataset/annotations/target-role-review-v1.jsonl")
    parser.add_argument("--sources", default="data/source_manifest.jsonl")
    parser.add_argument("--output", required=True)
    parser.add_argument("--steps", type=int, default=800)
    parser.add_argument("--batch", type=int, default=8)
    parser.add_argument("--seed", type=int, default=3070)
    parser.add_argument("--resume", help="Previously trained state_dict checkpoint; its hash is recorded")
    args = parser.parse_args()
    output = Path(args.output)
    if output.exists():
        raise ValueError("Use a new immutable training output directory")
    output.mkdir(parents=True)
    torch.set_num_threads(2); random.seed(args.seed); np.random.seed(args.seed); torch.manual_seed(args.seed)
    rows, images = load_data(args.annotations, args.sources)
    train_indices = [i for i, row in enumerate(rows) if row["split"] == "train"]
    if not train_indices:
        raise ValueError("No actual reviewed training frames")
    model = RoleDetector().train()
    if args.resume:
        model.load_state_dict(torch.load(args.resume, map_location="cpu", weights_only=True)["state_dict"])
    learning_rate = 0.0008 if args.resume else 0.002
    optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate, weight_decay=0.0001)
    losses = []
    start = time.perf_counter()
    for step in range(args.steps):
        # Balance visible hostile actors against traversal/NPC negatives without
        # promoting masked or unknown enemy observations to positive labels.
        enemy_indices = [i for i in train_indices if rows[i]["role_reviewed"]["Enemy"] and
                         any(obj["role"] == "Enemy" for obj in rows[i]["objects"])]
        indices = random.choices(train_indices, k=args.batch//2) + random.choices(enemy_indices or train_indices, k=args.batch-args.batch//2)
        batch, heat, boxes, positive, reviewed = transformed_batch(images, rows, indices, augment=step%4!=0)
        for group in optimizer.param_groups:
            group["lr"] = learning_rate*(0.25+0.75*(1+math.cos(math.pi*step/args.steps))/2)
        optimizer.zero_grad(set_to_none=True)
        loss = loss_value(model.raw(batch), heat, boxes, positive, reviewed)
        if not torch.isfinite(loss):
            raise ValueError("Nonfinite target training loss")
        loss.backward(); torch.nn.utils.clip_grad_norm_(model.parameters(), 5); optimizer.step()
        losses.append(float(loss.detach()))
        if step % 100 == 0 or step+1 == args.steps:
            print(json.dumps({"step": step+1, "loss": losses[-1], "elapsed_s": round(time.perf_counter()-start, 1)}), flush=True)
    model.eval()
    torch.save({"state_dict": model.state_dict(), "architecture": "dense-visible-role-v1", "seed": args.seed}, output/"targets.pt")
    onnx_path = output/"targets.onnx"
    torch.onnx.export(model, images[:1], onnx_path, input_names=["frame"], output_names=["scores", "boxes"],
                      opset_version=17, dynamo=False)
    graph = onnx.load(onnx_path)
    metadata = {
        "svai.contract": "target-roles-v1", "svai.preprocess": "full-rgb-bilinear-v1",
        "svai.model_version": "visible-roles-experimental-v1", "svai.roles": "Wolf,Enemy",
        "svai.training_status": "trained", "svai.semantic_supported": "true",
        "svai.production_validated": "false", "svai.score_threshold": "0.75",
        "svai.annotation_sha256": digest(args.annotations), "svai.annotation_review": "AI_VISUAL_REVIEW_NOT_HUMAN_GOLD",
        "svai.generalization": "NOT_ESTABLISHED", "svai.calibrated": "false",
    }
    for key, value in metadata.items():
        entry = graph.metadata_props.add(); entry.key = key; entry.value = value
    onnx.checker.check_model(graph); onnx.save(graph, onnx_path)
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    session = ort.InferenceSession(str(onnx_path), sess_options=options, providers=["CPUExecutionProvider"])
    parity = []
    with torch.no_grad():
        for i in [0, len(images)//2, len(images)-1]:
            native = session.run(None, {"frame": images[i:i+1].numpy()})
            reference = [value.numpy() for value in model(images[i:i+1])]
            for actual, expected in zip(native, reference):
                np.testing.assert_allclose(actual, expected, atol=2e-5, rtol=2e-4)
            parity.append(max(float(np.max(np.abs(a-b))) for a, b in zip(native, reference)))
    metrics, predictions, latency = evaluate(session, rows, images)
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        commit = "unknown"
    report = {"deployment_status": "EXPERIMENTAL_NOT_PRODUCTION_VALIDATED", "trained_on_actual_gameplay": True,
              "source_groups": sorted({row["source_group_id"] for row in rows}), "reviewed_frames": len(rows),
              "annotation_method": "AI_VISUAL_REVIEW_NOT_INDEPENDENT_HUMAN_GOLD", "source_commit": commit,
              "annotation_sha256": digest(args.annotations), "onnx_sha256": digest(onnx_path),
              "training_steps": args.steps, "seed": args.seed, "parameters": sum(p.numel() for p in model.parameters()),
              "resume_checkpoint_sha256": digest(args.resume) if args.resume else None,
              "loss_first_50_mean": float(np.mean(losses[:50])), "loss_last_50_mean": float(np.mean(losses[-50:])),
              "fitting_metrics_only": metrics, "held_out_accuracy": None,
              "no_held_out_reason": "Initial review is one conservatively grouped source family; no random-frame leakage split was made.",
              "cpu_model_ms_shared_development_host": latency, "pytorch_ort_parity_max_abs": parity,
              "training_seconds": time.perf_counter()-start, "versions": {"torch": torch.__version__, "onnxruntime": ort.__version__},
              "thresholds": {"score": 0.75, "nms_iou": 0.4, "matching_iou": 0.5},
              "limitations": ["No all-boss coverage", "No independent human ground truth", "No RTX 3070 live-game benchmark", "Scores uncalibrated"]}
    (output/"report.json").write_text(json.dumps(report, indent=2)+"\n")
    (output/"predictions.jsonl").write_text("".join(json.dumps(row)+"\n" for row in predictions))
    (output/"training-loss.json").write_text(json.dumps(losses)+"\n")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()
