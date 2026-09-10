"""Annotation v2 semantic validation; no-hit does not reveal contact TTI."""

from __future__ import annotations

import math
from typing import Any
import uuid

from DatasetTools.common import utc_now
from DatasetTools.annotation.taxonomy import (ATTACK_TYPES, ATTACK_DIRECTIONS, DIRECTIONS, IMPACT_EVIDENCE,
                                              MOVEMENTS, NEGATIVE_REASONS, PHASES, STATES)

BOUNDARIES = ("windup_start", "active_start", "impact_frame", "recovery_start",
              "recovery_end", "dodge_start")


def new_annotation(clip: dict, *, reviewer: str = "") -> dict:
    annotation_id = str(uuid.uuid4())
    return {"schema_version": "2.0", "annotation_id": annotation_id,
            "source_id": clip["source_id"], "source_group_id": clip["source_group_id"],
            "clip_id": clip["clip_id"], "event_id": annotation_id, "strike_id": annotation_id,
            "boss": clip.get("boss", "UNKNOWN"), "boss_phase": "UNKNOWN", "boss_attack_name": "",
            "clip_start": clip["start_source_frame"], "clip_end": clip["end_source_frame"],
            "anchor_frame": clip["start_source_frame"],
            "fps_num": clip["fps_num"], "fps_den": clip["fps_den"],
            "state": "NON_THREAT", "movement": "UNKNOWN_MOVEMENT", "phase": "UNKNOWN_PHASE",
            "attack_type": "UNKNOWN_ATTACK", "scope": "EVENT", "negative_reason": None,
            "threat_label": None, "windup_start": None, "active_start": None, "impact_frame": None,
            "recovery_start": None, "recovery_end": None, "impact_evidence": "OUT_OF_CLIP",
            "estimated_contact_interval": None, "dodge_start": None, "dodge_direction": "UNKNOWN",
            "attack_direction": "UNKNOWN", "attack_direction_evidence": "NOT_OBSERVED",
            "attack_direction_space": "SCREEN_WITH_WOLF_REFERENCE",
            "dodge_timing_evidence": "NOT_OBSERVED", "direction_space": "SCREEN",
            "would_hit_without_dodge": "UNKNOWN", "confidence": 0.8,
            "roi": clip.get("roi", [0.20, 0.12, 0.82, 0.82]),
            "annotation_status": "proposed", "annotator": reviewer, "reviewer": "",
            "reviewed_at": None, "created_at": utc_now(), "revision": 0,
            "example_only": bool(clip.get("example_only", False)), "notes": ""}


def _integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def validate_annotation(row: dict, clip: dict | None = None) -> None:
    required = ("annotation_id", "source_id", "source_group_id", "clip_id", "event_id", "strike_id",
                "boss", "clip_start", "clip_end", "anchor_frame", "state", "movement", "phase",
                "attack_type", "scope", "impact_evidence", "annotation_status", "confidence", "revision")
    missing = [key for key in required if key not in row]
    if missing:
        raise ValueError("Annotation missing: " + ", ".join(missing))
    if row.get("schema_version") != "2.0":
        raise ValueError("Use annotation schema_version 2.0; v1 examples are documentation only.")
    begin, end, anchor = row["clip_start"], row["clip_end"], row["anchor_frame"]
    if not all(_integer(value) for value in (begin, end, anchor)) or not 0 <= begin <= anchor < end:
        raise ValueError("Absolute source-frame clip interval is half-open; anchor must lie inside it.")
    for key in BOUNDARIES:
        value = row.get(key)
        if value is not None and (not _integer(value) or value < begin or
                                   (value > end if key == "recovery_end" else value >= end)):
            raise ValueError(f"{key} must be an absolute source frame in this clip, or null.")
    ordered = [row.get(key) for key in ("windup_start", "active_start", "recovery_start", "recovery_end")]
    present = [value for value in ordered if value is not None]
    if present != sorted(present):
        raise ValueError("Require windup_start <= active_start <= recovery_start <= recovery_end.")
    for key, choices in (("state", STATES), ("attack_type", ATTACK_TYPES), ("movement", MOVEMENTS),
                         ("phase", PHASES), ("impact_evidence", IMPACT_EVIDENCE),
                         ("dodge_direction", DIRECTIONS)):
        if row.get(key) not in choices:
            raise ValueError(f"Invalid {key}: {row.get(key)!r}")
    if row["scope"] not in ("EVENT", "ENTIRE_CLIP_NON_THREAT"):
        raise ValueError("scope must be EVENT or ENTIRE_CLIP_NON_THREAT.")
    if row.get("threat_label") is not None and not isinstance(row["threat_label"], bool):
        raise ValueError("threat_label is reviewed true/false, or null for unknown.")
    if row["scope"] == "ENTIRE_CLIP_NON_THREAT":
        if row.get("negative_reason") not in NEGATIVE_REASONS or row.get("threat_label") is not False:
            raise ValueError("An entire-clip negative requires a reviewed reason and threat_label=false.")
        if any(row.get(key) is not None for key in BOUNDARIES[:5]):
            raise ValueError("An entire-clip negative cannot also contain an annotated attack/contact.")
        if row["state"] in ("ATTACK_WINDUP", "ACTIVE_ATTACK", "COMBO_CONTINUATION"):
            raise ValueError("An entire-clip negative cannot have an attack state.")
    impact = row.get("impact_frame")
    interval = row.get("estimated_contact_interval")
    if row["impact_evidence"] == "OBSERVED_CONTACT":
        if impact is None or row.get("active_start") is None:
            raise ValueError("Observed contact needs an actual impact frame and active_start.")
        if impact < row["active_start"] or (row.get("recovery_start") is not None and impact >= row["recovery_start"]):
            raise ValueError("Observed contact must lie in the active interval, before recovery.")
        if interval is not None:
            raise ValueError("Observed point contact must not also carry a counterfactual interval.")
    elif impact is not None:
        raise ValueError("Only OBSERVED_CONTACT may provide impact_frame; no-hit/occluded TTI stays censored.")
    if row["impact_evidence"] == "ESTIMATED_COUNTERFACTUAL":
        if (not isinstance(interval, list) or len(interval) != 2 or not all(_integer(v) for v in interval)
                or not begin <= interval[0] <= interval[1] < end):
            raise ValueError("Counterfactual contact requires an explicitly reviewed source-frame interval.")
    elif interval is not None:
        raise ValueError("Only ESTIMATED_COUNTERFACTUAL may carry estimated_contact_interval.")
    if row.get("dodge_start") is None:
        if row.get("dodge_timing_evidence") != "NOT_OBSERVED" or row.get("dodge_direction") != "UNKNOWN":
            raise ValueError("Unknown Dodge time must not carry observed input/direction labels.")
    elif row.get("dodge_timing_evidence") not in ("INPUT_LOG", "VISUAL_ONSET"):
        raise ValueError("Dodge start requires INPUT_LOG or VISUAL_ONSET evidence.")
    if row.get("direction_space") != "SCREEN":
        raise ValueError("v2 direction labels use screen coordinates, not a proven safe-action target.")
    # Optional v2 extension: old journals remain readable. A player's observed
    # dodge never supplies this separate weapon/attack trajectory label.
    attack_direction = row.get("attack_direction", "UNKNOWN")
    attack_evidence = row.get("attack_direction_evidence", "NOT_OBSERVED")
    if attack_direction not in ATTACK_DIRECTIONS:
        raise ValueError("Invalid attack_direction.")
    if attack_evidence not in ("VISUAL_TRAJECTORY", "NOT_OBSERVED"):
        raise ValueError("Attack direction needs reviewed VISUAL_TRAJECTORY evidence or NOT_OBSERVED.")
    if (attack_direction == "UNKNOWN") != (attack_evidence == "NOT_OBSERVED"):
        raise ValueError("Unknown attack direction must remain unobserved; known direction needs visible trajectory.")
    if row.get("attack_direction_space", "SCREEN_WITH_WOLF_REFERENCE") != "SCREEN_WITH_WOLF_REFERENCE":
        raise ValueError("Attack direction uses screen axes, with Wolf reference for toward/away labels.")
    if attack_direction != "UNKNOWN" and row["scope"] == "ENTIRE_CLIP_NON_THREAT":
        raise ValueError("A non-threat interval cannot supply an attack trajectory label.")
    if row.get("would_hit_without_dodge") not in ("TRUE", "FALSE", "UNKNOWN"):
        raise ValueError("would_hit_without_dodge is TRUE, FALSE, or UNKNOWN.")
    quality = row["confidence"]
    if isinstance(quality, bool) or not isinstance(quality, (int, float)) or not math.isfinite(quality) or not 0 <= quality <= 1:
        raise ValueError("Annotation confidence must be finite and between 0 and 1.")
    if not _integer(row["revision"]) or row["revision"] < 0:
        raise ValueError("Annotation revision must be a non-negative integer.")
    if row["annotation_status"] not in ("proposed", "reviewed", "rejected"):
        raise ValueError("Annotation status must distinguish proposals, reviewed labels, and rejections.")
    if row["annotation_status"] == "reviewed" and (not row.get("reviewer", "").strip() or not row.get("reviewed_at")):
        raise ValueError("Accepted labels need an identified reviewer and review timestamp.")
    roi = row.get("roi")
    if (not isinstance(roi, list) or len(roi) != 4 or
            not all(isinstance(v, (int, float)) and math.isfinite(v) for v in roi) or
            not (0 <= roi[0] < roi[2] <= 1 and 0 <= roi[1] < roi[3] <= 1)):
        raise ValueError("ROI must be normalized [left, top, right, bottom].")
    if clip is not None:
        if row["clip_id"] != clip["clip_id"] or row["source_id"] != clip["source_id"]:
            raise ValueError("Annotation source/clip identity does not match its media manifest.")
        if begin != clip["start_source_frame"] or end != clip["end_source_frame"]:
            raise ValueError("Annotation interval must match the clip's original source-frame extent.")
        if bool(row.get("example_only")) != bool(clip.get("example_only")):
            raise ValueError("Synthetic-test provenance must never be removed from an annotation.")


def state_at(row: dict, source_frame: int) -> str | None:
    """Expand only reviewed intervals; do not invent negatives before windup."""
    if not row["clip_start"] <= source_frame < row["clip_end"]:
        return None
    if row["scope"] == "ENTIRE_CLIP_NON_THREAT":
        return row["state"]
    windup, active, recovery, end = (row.get(key) for key in
                                     ("windup_start", "active_start", "recovery_start", "recovery_end"))
    if windup is not None and active is not None and windup <= source_frame < active:
        return "ATTACK_WINDUP"
    if active is not None and recovery is not None and active <= source_frame < recovery:
        return "ACTIVE_ATTACK"
    if recovery is not None and end is not None and recovery <= source_frame < end:
        return "RECOVERY"
    # A scalar class is reviewable at exactly its explicit anchor; it is not a
    # license to propagate that class over every unannotated frame in the clip.
    if source_frame == row["anchor_frame"] and row["phase"] != "UNKNOWN_PHASE":
        return row["state"]
    return None
