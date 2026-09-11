"""One-to-one matching of causal attack detections to reviewed source events."""
import argparse
import json
from pathlib import Path

import numpy as np


def evaluate_events(events, detections, non_threat_minutes=None):
    """Intervals must be reviewed [windup_ms, active_end_ms] in SOURCE time.

    Detections are emitted episode tokens, not every frame above threshold.
    Unknown/censored contact may support attack event matching, never TTI error.
    """
    matches, used = [], set()
    detections = sorted(detections, key=lambda row: (row["source_id"], row["source_ms"]))
    evaluable = [e for e in events if e.get("reviewed") is True]
    for detection in detections:
        candidates = [(index, event) for index, event in enumerate(evaluable)
                      if index not in used and event["source_id"] == detection["source_id"]
                      and event["windup_ms"] <= detection["source_ms"] <= event["active_end_ms"]]
        if candidates:
            index, event = min(candidates, key=lambda item: item[1]["active_end_ms"])
            used.add(index)
            matches.append((detection, event))
    tp, fp, fn = len(matches), len(detections)-len(matches), len(evaluable)-len(matches)
    lead = [e["impact_ms"]-d["source_ms"] for d, e in matches
            if e.get("impact_evidence") == "OBSERVED_CONTACT" and e.get("impact_ms") is not None]
    decisions = [d for d in detections if d.get("dodge_sent") is True]
    # Matching an attack is not proof a Dodge was necessary. False-Dodge labels
    # require reviewed decisions under the stated threat semantics.
    reviewed_decisions = [d for d in decisions if type(d.get("reviewed_threat")) is bool]
    false_decisions = sum(d["reviewed_threat"] is False for d in reviewed_decisions)
    return {"evaluable_events": len(evaluable), "detections": len(detections), "tp": tp, "fp": fp, "fn": fn,
            "event_precision": tp/(tp+fp) if tp+fp else None, "event_recall": tp/(tp+fn) if tp+fn else None,
            "event_f1": 2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else None,
            "missed_attack_fraction": fn/len(evaluable) if evaluable else None,
            "observed_impact_lead_p50_ms": float(np.median(lead)) if lead else None,
            "observed_impact_lead_p10_ms": float(np.quantile(lead, .1)) if lead else None,
            "reviewed_dodge_decisions": len(reviewed_decisions), "unreviewed_dodge_decisions": len(decisions)-len(reviewed_decisions),
            "false_dodges": false_decisions if reviewed_decisions else None,
            "false_dodge_fraction_of_reviewed_decisions": false_decisions/len(reviewed_decisions) if reviewed_decisions else None,
            "false_dodges_per_non_threat_minute": false_decisions/non_threat_minutes
                if reviewed_decisions and len(reviewed_decisions) == len(decisions) and non_threat_minutes else None,
            "non_threat_minutes": non_threat_minutes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("events", type=Path)
    parser.add_argument("detections", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--non-threat-minutes", type=float)
    args = parser.parse_args()
    if args.non_threat_minutes is not None and args.non_threat_minutes <= 0:
        parser.error("non-threat-minutes must be positive measured exposure")
    read = lambda path: [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    result = evaluate_events(read(args.events), read(args.detections), args.non_threat_minutes)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
