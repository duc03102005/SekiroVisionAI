#!/usr/bin/env python3
"""Summarize M1 capture evidence. This cannot certify the full hardware milestone."""

import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path


def percentiles(values):
    values = sorted(v for v in values if math.isfinite(v) and v >= 0)
    if not values:
        return {key: None for key in ("p50", "p95", "p99", "max")}
    return {
        "p50": values[math.ceil(0.50 * len(values)) - 1],
        "p95": values[math.ceil(0.95 * len(values)) - 1],
        "p99": values[math.ceil(0.99 * len(values)) - 1],
        "max": values[-1],
    }


def number(row, field):
    return float(row[field]) if row[field] else math.nan


def analyze(path, warmup_seconds, duration_seconds):
    if not math.isfinite(warmup_seconds) or warmup_seconds < 0:
        raise ValueError("Warmup must be finite and nonnegative")
    if not math.isfinite(duration_seconds) or duration_seconds <= 0:
        raise ValueError("Duration must be finite and positive")
    meta_path = Path(str(path) + ".meta.json")
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    if meta["schema_version"] != 1:
        raise ValueError("Unsupported trace metadata schema")
    start = float(meta["start_qpc_ms"]) + warmup_seconds * 1000
    finish = start + duration_seconds * 1000
    with path.open(newline="", encoding="utf-8") as stream:
        all_rows = list(csv.DictReader(stream))
    sequences = [int(row["sequence"]) for row in all_rows]
    if sequences != list(range(1, len(all_rows) + 1)):
        raise ValueError("Trace sequences are missing, repeated or out of order")
    rows = [row for row in all_rows if start <= number(row, "dequeue_qpc_ms") < finish]
    copied = [row for row in rows if row["outcome"] == "copied"]
    outcomes = dict(collections.Counter(row["outcome"] for row in rows))
    drops = sum(outcomes.get(key, 0) for key in ("superseded", "ring_busy", "resize", "invalid", "abandoned", "pending"))
    windows = []
    for offset in range(math.ceil(duration_seconds / 10)):
        begin = start + offset * 10000
        end = min(begin + 10000, finish)
        count = sum(begin <= number(row, "dequeue_qpc_ms") < end for row in rows)
        windows.append({"offset_seconds": offset * 10, "fps": count * 1000 / (end - begin)})
    arrivals = [number(row, "dequeue_qpc_ms") for row in rows]
    gaps = [b - a for a, b in zip([start] + arrivals, arrivals + [finish])]
    source = [number(row, "source_qpc_ms") for row in rows]
    source_intervals = [b - a for a, b in zip(source, source[1:]) if b > a]
    dequeue_age = [number(row, "dequeue_qpc_ms") - number(row, "source_qpc_ms") for row in rows]
    ready_age = [number(row, "ready_observed_qpc_ms") - number(row, "source_qpc_ms") for row in copied]
    complete = float(meta["end_qpc_ms"]) >= finish and not meta["trace_full"] and len(all_rows) == meta["received"]
    loss = drops / len(rows) if rows else None
    return {
        "scope": "Capture trace only; full M1 acceptance requires live configuration, source-FPS baseline, resource usage and lifecycle evidence",
        "trace_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "metadata_sha256": hashlib.sha256(meta_path.read_bytes()).hexdigest(),
        "build_revision": meta["build_revision"],
        "adapter": meta["adapter"],
        "warmup_seconds": warmup_seconds,
        "duration_seconds": duration_seconds,
        "recording_covers_requested_window": complete,
        "final_state": meta["final_state"],
        "received": len(rows), "completed": len(copied), "outcomes": outcomes,
        "delivered_fps": len(rows) / duration_seconds,
        "copy_fps": len(copied) / duration_seconds,
        "intentional_or_invalid_loss_fraction": loss,
        "ten_second_windows": windows,
        "source_interval_ms": percentiles(source_intervals),
        "dequeue_age_ms": percentiles(dequeue_age),
        "gpu_ready_observed_age_ms": percentiles(ready_age),
        "gpu_copy_ms": percentiles(number(row, "gpu_copy_ms") for row in copied),
        "missing_gpu_times": sum(not math.isfinite(number(row, "gpu_copy_ms")) for row in copied),
        "ingress_gaps_over_100_ms": sum(gap > 100 for gap in gaps),
        "max_ingress_gap_ms": max(gaps, default=None),
        "callback_gaps_entire_session": meta["callback_gaps_over_100_ms"],
        "peak_ring_slots_entire_session": meta["peak_in_flight"],
        "capture_criteria_observed": {
            "complete_600_second_window_after_30_second_warmup": complete and duration_seconds >= 600 and warmup_seconds >= 30,
            "all_windows_at_least_59_4_fps": bool(rows) and all(window["fps"] >= 59.4 for window in windows),
            "loss_at_most_one_percent": loss is not None and loss <= 0.01,
            "no_ingress_gap_over_100_ms": bool(rows) and all(gap <= 100 for gap in gaps),
            "normal_stop": meta["final_state"] == "Stopped",
        },
        "full_milestone_result": "NOT_EVALUATED",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--warmup-seconds", type=float, default=30)
    parser.add_argument("--duration-seconds", type=float, default=600)
    args = parser.parse_args()
    try:
        result = analyze(args.trace, args.warmup_seconds, args.duration_seconds)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
