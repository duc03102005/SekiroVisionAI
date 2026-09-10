"""Prioritize reviewed hard negatives and unreviewed pseudo-label suggestions."""
import argparse
import json
from pathlib import Path


def review_queue(rows, threshold=0.8, uncertainty_band=0.1):
    proposals = []
    for row in rows:
        threat = float(row["threat_probability"])
        attack = float(row["attack_probability"])
        labels = row.get("labels", {})
        if threat >= threshold and labels.get("threat") is False:
            reason, priority = "high_threat_on_reviewed_negative", 3+threat
        elif max(threat, attack) >= threshold and (labels.get("threat") is None or labels.get("attack") is None):
            reason, priority = "high_score_unreviewed_candidate", 1+max(threat, attack)
        elif abs(threat-0.5) <= uncertainty_band or abs(attack-0.5) <= uncertainty_band:
            reason, priority = "uncertain_model_candidate", 2-abs(threat-0.5)
        else:
            continue
        proposals.append({"source_id": row["source_id"], "clip_id": row["clip_id"],
                          "annotation_id": row.get("annotation_id"), "source_pts_ms": row.get("source_pts_ms"),
                          "proposal_only": True, "annotation_status": "needs_review", "reason": reason,
                          "priority": priority, "attack_probability": attack, "threat_probability": threat,
                          "proposed_labels": {}, "previous_reviewed_labels": labels,
                          "instruction": "Review video, accept or reject; never turn a model score into ground truth."})
    return sorted(proposals, key=lambda row: -row["priority"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("predictions", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--threshold", type=float, default=0.8)
    args = parser.parse_args()
    if not 0 < args.threshold < 1:
        parser.error("threshold must lie in (0,1)")
    rows = [json.loads(line) for line in args.predictions.read_text(encoding="utf-8").splitlines() if line.strip()]
    proposals = review_queue(rows, args.threshold)
    with args.output.open("x", encoding="utf-8") as stream:
        for proposal in proposals:
            stream.write(json.dumps(proposal)+"\n")
    print(json.dumps({"review_candidates": len(proposals), "accepted_labels_written": 0}))


if __name__ == "__main__":
    main()
