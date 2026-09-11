"""Explicit denominators: clip-anchor scores are not event or live Dodge accuracy."""
import math
import numpy as np


def binary_metrics(targets, probabilities, threshold):
    if not targets:
        return {"evaluable_samples": 0, "precision": None, "recall": None, "f1": None,
                "false_positive_fraction_of_negatives": None, "missed_positive_fraction": None}
    y, predicted = np.asarray(targets, bool), np.asarray(probabilities) >= threshold
    tp, fp, fn, tn = (int(np.sum(item)) for item in (y & predicted, ~y & predicted, y & ~predicted, ~y & ~predicted))
    precision, recall = tp/(tp+fp) if tp+fp else None, tp/(tp+fn) if tp+fn else None
    return {"evaluable_samples": len(targets), "threshold": threshold, "tp": tp, "fp": fp, "fn": fn, "tn": tn,
            "precision": precision, "recall": recall, "f1": 2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else None,
            "false_positive_fraction_of_negatives": fp/(fp+tn) if fp+tn else None,
            "missed_positive_fraction": fn/(fn+tp) if fn+tp else None,
            "brier": float(np.mean((np.asarray(probabilities)-y)**2))}


def summarize_predictions(rows, thresholds=None, include_groups=True, supported_heads=None):
    thresholds = thresholds or {"attack": 0.8, "threat": 0.85}
    result = {"metric_unit": "reviewed clip anchor; not live events", "sample_count": len(rows)}
    for head in ("attack", "threat"):
        usable = [r for r in rows if r.get("labels", {}).get(head) is not None]
        supported = supported_heads is None or bool(supported_heads.get(head+"_supported"))
        result[head] = binary_metrics([r["labels"][head] for r in usable] if supported else [],
                                     [r[f"{head}_probability"] for r in usable] if supported else [], thresholds[head])
        result[head].update(model_head_supported=supported, available_label_count=len(usable))
    timing = [r for r in rows if r.get("labels", {}).get("tti_ms") is not None
              and not r["labels"].get("tti_censored", True)
              and r["labels"].get("impact_evidence") == "OBSERVED_CONTACT"]
    timing_supported = supported_heads is None or bool(supported_heads.get("tti_supported"))
    if not timing_supported:
        timing = []
    error = np.asarray([r["tti_ms"]-r["labels"]["tti_ms"] for r in timing])
    result["tti"] = {"observed_samples": len(timing), "censored_or_unsupported_samples": len(rows)-len(timing),
                     "model_head_supported": timing_supported,
                     "mae_ms": float(np.abs(error).mean()) if timing else None,
                     "p90_absolute_error_ms": float(np.quantile(np.abs(error), 0.90)) if timing else None,
                     "signed_bias_ms": float(error.mean()) if timing else None,
                     "nominal_90pct_laplace_interval_coverage": float(np.mean([
                         abs(e) <= r["tti_uncertainty_ms"]*math.log(10) for e, r in zip(error, timing)])) if timing else None}
    directions = [r for r in rows if r.get("labels", {}).get("attack_direction") not in (None, "UNKNOWN")
                  and r["labels"].get("attack_direction_evidence") == "VISUAL_TRAJECTORY"
                  and r["labels"].get("attack_direction_space") == "SCREEN_WITH_WOLF_REFERENCE"]
    direction_supported = supported_heads is None or bool(supported_heads.get("attack_direction_supported"))
    if not direction_supported:
        directions = []
    result["attack_direction"] = {
        "observed_trajectory_samples": len(directions),
        "model_head_supported": direction_supported,
        "accuracy": sum(r.get("attack_direction_prediction") == r["labels"]["attack_direction"]
                        for r in directions)/len(directions) if directions else None,
        "semantics": "Visual attack path; this is not a safe Dodge-direction success metric"}
    # A successful ORT invocation cannot supply these gameplay facts.
    result["live_false_dodge_rate"] = None
    result["live_end_to_end_latency_ms"] = None
    result["event_metrics"] = "Use Evaluation.offline.events with reviewed event intervals and emitted decisions."
    if include_groups:
        for field in ("boss", "attack_class"):
            groups = {}
            for row in rows:
                key = row.get("boss", "UNKNOWN") if field == "boss" else row.get("labels", {}).get("class") or "UNKNOWN"
                groups.setdefault(key, []).append(row)
            result[f"per_{field}"] = {key: summarize_predictions(group, thresholds, False, supported_heads) for key, group in groups.items()}
    return result


def choose_threshold(rows, head, maximum_false_positive_fraction=0.01):
    """Validation only; precision-first policy with an explicit target, no guarantee."""
    usable = [r for r in rows if r.get("labels", {}).get(head) is not None]
    targets = [r["labels"][head] for r in usable]
    if not any(targets) or all(targets):
        return 0.85, {"status": "insufficient_validation_classes", "threshold": 0.85}
    probabilities = [r[f"{head}_probability"] for r in usable]
    candidates = []
    for threshold in np.linspace(0.50, 0.99, 50):
        metrics = binary_metrics(targets, probabilities, float(threshold))
        if metrics["false_positive_fraction_of_negatives"] <= maximum_false_positive_fraction:
            candidates.append(metrics)
    if not candidates:
        return 0.99, {"status": "target_not_met", "threshold": 0.99}
    selected = max(candidates, key=lambda m: (m["recall"] or 0, m["precision"] or 0, m["threshold"]))
    return selected["threshold"], {"status": "validation_selected_not_live_guarantee", **selected}
