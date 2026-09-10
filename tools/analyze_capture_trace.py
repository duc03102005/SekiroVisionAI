#!/usr/bin/env python3
"""Summarize matched capture/source/GPU/consumer evidence (trace v1 or v2).

Capture callback cadence is not consumer freshness or proof of game behavior.
"""

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
    return float(row[field]) if row.get(field) else math.nan


def analyze(path, warmup_seconds, duration_seconds):
    if not math.isfinite(warmup_seconds) or warmup_seconds < 0:
        raise ValueError("Warmup must be finite and nonnegative")
    if not math.isfinite(duration_seconds) or duration_seconds <= 0:
        raise ValueError("Duration must be finite and positive")
    meta_path = Path(str(path) + ".meta.json")
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    if meta["schema_version"] not in (1, 2):
        raise ValueError("Unsupported trace metadata schema")
    start = float(meta["start_qpc_ms"]) + warmup_seconds * 1000
    finish = start + duration_seconds * 1000
    with path.open(newline="", encoding="utf-8") as stream:
        all_rows = list(csv.DictReader(stream))
    sequences = [int(row["sequence"]) for row in all_rows]
    if sequences != list(range(1, len(all_rows) + 1)):
        raise ValueError("Trace sequences are missing, repeated or out of order")
    def in_window(row, field):
        return start <= number(row, field) < finish

    rows = [row for row in all_rows if in_window(row, "dequeue_qpc_ms")]
    # The compositor source timestamp must advance. A callback/dequeue that
    # repeats or invents source time cannot preserve a healthy capture FPS.
    valid_sources = []
    last_source = 0.0
    for row in all_rows:
        source = number(row, "source_qpc_ms")
        dequeue = number(row, "dequeue_qpc_ms")
        if math.isfinite(source) and source > last_source and source <= dequeue:
            valid_sources.append(row)
            last_source = source
    valid_in_window = [row for row in valid_sources if in_window(row, "dequeue_qpc_ms")]
    # In v2, a copied image can be superseded or age out before reaching the
    # consumer. Its GPU event still counts as a completed GPU copy.
    copied = [row for row in all_rows if in_window(row, "ready_observed_qpc_ms")]
    if meta["schema_version"] == 1:
        copied = [row for row in copied if row["outcome"] == "copied"]
    delivered = [row for row in all_rows if in_window(row, "sink_return_qpc_ms")]
    cpu_copied = [row for row in all_rows if in_window(row, "consumer_ready_qpc_ms")]
    outcomes = dict(collections.Counter(row["outcome"] for row in rows))
    drops = sum(count for outcome, count in outcomes.items() if outcome != "copied")
    windows = []
    for offset in range(math.ceil(duration_seconds / 10)):
        begin = start + offset * 10000
        end = min(begin + 10000, finish)
        count = sum(begin <= number(row, "dequeue_qpc_ms") < end for row in valid_in_window)
        windows.append({"offset_seconds": offset * 10, "valid_source_fps": count * 1000 / (end - begin)})
    arrivals = [number(row, "dequeue_qpc_ms") for row in valid_in_window]
    gaps = [b - a for a, b in zip([start] + arrivals, arrivals + [finish])]
    source = [number(row, "source_qpc_ms") for row in valid_in_window]
    source_intervals = [b - a for a, b in zip(source, source[1:])]
    dequeue_age = [number(row, "dequeue_qpc_ms") - number(row, "source_qpc_ms") for row in rows]
    ready_age = [number(row, "ready_observed_qpc_ms") - number(row, "source_qpc_ms") for row in copied]
    consumer_age = [number(row, "consumer_ready_qpc_ms") - number(row, "source_qpc_ms") for row in cpu_copied]
    sink_age = [number(row, "sink_return_qpc_ms") - number(row, "source_qpc_ms") for row in delivered]
    bytes_read = sum(int(row.get("cpu_readback_bytes") or 0) for row in cpu_copied)
    delivered_order = sorted(delivered, key=lambda row: number(row, "sink_return_qpc_ms"))
    ordering_errors = sum(
        int(b["sequence"]) <= int(a["sequence"]) or number(b, "source_qpc_ms") <= number(a, "source_qpc_ms")
        for a, b in zip(delivered_order, delivered_order[1:])
    )

    complete = float(meta["end_qpc_ms"]) >= finish and not meta["trace_full"] and len(all_rows) == meta["received"]
    loss = drops / len(rows) if rows else None
    return {
        "scope": "Matched capture trace only; consumer freshness is independent of callback FPS. This does not establish successful Sekiro Dodge or RTX performance.",
        "trace_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "metadata_sha256": hashlib.sha256(meta_path.read_bytes()).hexdigest(),
        "build_revision": meta["build_revision"],
        "adapter": meta["adapter"],
        "warmup_seconds": warmup_seconds,
        "duration_seconds": duration_seconds,
        "recording_covers_requested_window": complete,
        "final_state": meta["final_state"],
        "received": len(rows), "completed": len(copied), "outcomes": outcomes,
        "received_fps": len(rows) / duration_seconds,
        "valid_source_fps": len(valid_in_window) / duration_seconds,
        "delivered_to_sink": len(delivered) if meta["schema_version"] == 2 else None,
        "delivered_fps": len(delivered) / duration_seconds if meta["schema_version"] == 2 else None,
        "copy_fps": len(copied) / duration_seconds,
        "intentional_or_invalid_loss_fraction": loss,
        "ten_second_windows": windows,
        "source_interval_ms": percentiles(source_intervals),
        "dequeue_age_ms": percentiles(dequeue_age),
        "gpu_ready_observed_age_ms": percentiles(ready_age),
        "consumer_ready_age_ms": percentiles(consumer_age),
        "sink_return_age_ms": percentiles(sink_age),
        "sink_duration_ms": percentiles(number(row, "sink_ms") for row in delivered),
        "delivery_ordering_errors": ordering_errors if meta["schema_version"] == 2 else None,
        "cpu_readback_bytes": bytes_read if meta["schema_version"] == 2 else None,
        "cpu_readback_bytes_per_second": bytes_read / duration_seconds if meta["schema_version"] == 2 else None,
        "gpu_copy_ms": percentiles(number(row, "gpu_copy_ms") for row in copied),
        "missing_gpu_times": sum(not math.isfinite(number(row, "gpu_copy_ms")) for row in copied),
        "ingress_gaps_over_100_ms": sum(gap > 100 for gap in gaps),
        "max_ingress_gap_ms": max(gaps, default=None),
        "callback_gaps_entire_session": meta["callback_gaps_over_100_ms"],
        "peak_ring_slots_entire_session": meta["peak_in_flight"],
        "normal_stop": meta["final_state"] == "Stopped",
        "live_game_validation": "NOT_ESTABLISHED_BY_CAPTURE_TRACE",
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
