"""Append-only annotation revisions, shared by the GUI and batch review tools."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path

from DatasetTools.common import append_jsonl, load_jsonl, utc_now
from DatasetTools.annotation.contract import new_annotation, validate_annotation


def latest_annotations(path: Path) -> list[dict]:
    latest: dict[str, dict] = {}
    for row in load_jsonl(path):
        validate_annotation(row)
        previous = latest.get(row["annotation_id"])
        if previous is not None and row["revision"] <= previous["revision"]:
            raise ValueError("Annotation journal revisions must increase monotonically per annotation ID.")
        latest[row["annotation_id"]] = row
    return list(latest.values())


def save_revision(path: Path, draft: dict, clip: dict, *, reviewer: str,
                  status: str = "proposed") -> dict:
    row = deepcopy(draft)
    previous = next((item for item in latest_annotations(path)
                     if item["annotation_id"] == row["annotation_id"]), None)
    if previous is not None and row["revision"] != previous["revision"]:
        raise ValueError("A newer annotation revision exists. Reload it before editing.")
    row["revision"] = (previous["revision"] if previous else 0) + 1
    row["annotation_status"] = status
    row["annotator"] = row.get("annotator") or reviewer
    row["reviewer"] = reviewer.strip() if status == "reviewed" else ""
    row["reviewed_at"] = utc_now() if status == "reviewed" else None
    row["modified_at"] = utc_now()
    validate_annotation(row, clip)
    append_jsonl(path, row)
    return row


def copy_semantic_labels(previous: dict, clip: dict, reviewer: str) -> dict:
    """Copy taxonomy/ROI, never contact times or accepted-review status."""
    row = new_annotation(clip, reviewer=reviewer)
    for field in ("boss_phase", "boss_attack_name", "state", "movement", "phase", "attack_type", "roi"):
        if field in previous:
            row[field] = deepcopy(previous[field])
    row["notes"] = f"Semantic proposal copied from {previous['annotation_id']}; boundaries and evidence require new review."
    return row
