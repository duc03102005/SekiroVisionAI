"""Recompute coverage from imported media and the latest reviewed annotations.

Run from the repository root:
    python -m DatasetTools.catalog.update_coverage

Missing input manifests mean zero acquired/reviewed coverage. Public catalog
links contribute only a discovery count in notes, never training coverage.
See DatasetTools/catalog/README.md for precise count and score definitions.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import json
import math
from pathlib import Path
import re

from DatasetTools.annotation.contract import validate_annotation
from DatasetTools.annotation.journal import latest_annotations
from DatasetTools.common import load_jsonl


COLUMNS = ("boss", "phase", "number_of_sources", "total_minutes", "attack_clips",
           "positive_attack_clips", "negative_clips", "different_players",
           "different_camera_conditions", "coverage_score", "status", "notes")
ATTACK_STATES = {"ATTACK_WINDUP", "ACTIVE_ATTACK", "COMBO_CONTINUATION"}
UNKNOWN = {"", "UNKNOWN", "UNREVIEWED", "UNKNOWN_BOSS", "UNKNOWN_PHASE", "N/A", "NONE"}
REAL_USAGE = {"OWN_RECORDING", "EXPLICIT_PERMISSION", "OPEN_LICENSE"}


def _csv(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def _index(rows: list[dict], key: str) -> dict[str, dict]:
    output = {}
    for row in rows:
        value = str(row.get(key, "")).strip()
        if not value or value in output:
            raise ValueError(f"Manifest has missing or duplicate {key}: {value!r}")
        output[value] = row
    return output


def _media_exists(value, manifest: Path) -> bool:
    if not isinstance(value, str) or not value:
        return False
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = manifest.parent / path
    return path.is_file() and path.stat().st_size > 0


def _known(value) -> str:
    text = str(value or "").strip()
    return "" if text.upper() in UNKNOWN else text


def _groups(sources: dict[str, dict]) -> dict[str, str]:
    """Collapse duplicate recordings by hash, duplicate group or original URL.

    Creator/session are intentionally not duplicate-media identifiers: one
    player can supply several genuinely different recording sources.
    """
    parent = {key: key for key in sources}

    def find(key):
        while parent[key] != key:
            parent[key] = parent[parent[key]]
            key = parent[key]
        return key

    seen = {}
    for identifier, source in sources.items():
        for field in ("sha256", "duplicate_group_id", "url"):
            value = _known(source.get(field))
            if not value:
                continue
            token = (field, value.casefold() if field == "sha256" else value)
            if token in seen:
                parent[find(identifier)] = find(seen[token])
            else:
                seen[token] = identifier
    return {key: find(key) for key in sources}


def _union_ms(intervals: list[tuple[float, float]]) -> float:
    end = -math.inf
    length = 0.0
    for start, finish in sorted(intervals):
        length += max(0.0, finish - max(start, end))
        end = max(end, finish)
    return length


def _score(sources: int, positives: int, negatives: int, players: int, cameras: int) -> float:
    # An operational inventory index, not accuracy or permission to send input.
    values = (sources / 3, positives / 50, negatives / 50, players / 3, cameras / 3)
    return round(20 * sum(min(1.0, value) for value in values), 2)


def build_coverage(*, catalog: Path, sources: Path, clips: Path, annotations: Path,
                   taxonomy: Path, existing: Path) -> tuple[list[dict], dict]:
    """Return deterministic rows and exclusions without writing any files."""
    definitions = json.loads(taxonomy.read_text(encoding="utf-8"))
    aliases = {}
    keys = []
    for boss in definitions["bosses"]:
        name = boss["name"]
        for alias in [name, *boss.get("aliases", [])]:
            aliases[alias.casefold()] = name
        keys.extend((name, phase) for phase in boss.get("phase_keys", ["UNREVIEWED"]))

    def canonical(value):
        name = _known(value)
        return aliases.get(name.casefold(), name)

    for row in _csv(existing):
        if _known(row.get("boss")):
            keys.append((canonical(row["boss"]), _known(row.get("phase")) or "UNREVIEWED"))
    references = Counter()
    for row in _csv(catalog):
        for boss in set(str(row.get("boss", "")).split(";")):
            if canonical(boss):
                references[canonical(boss)] += 1

    excluded = Counter()
    imported = {}
    for identifier, source in _index(load_jsonl(sources), "source_id").items():
        if source.get("example_only") or source.get("usage_basis") == "SYNTHETIC_TEST":
            excluded["synthetic_sources"] += 1
        elif (source.get("download_status") != "IMPORTED"
              or source.get("usage_basis") not in REAL_USAGE
              or not str(source.get("usage_evidence", "")).strip()
              or not re.fullmatch(r"[0-9a-fA-F]{64}", str(source.get("sha256", "")))):
            excluded["sources_without_import_provenance"] += 1
        elif not _media_exists(source.get("media_path"), sources):
            excluded["sources_without_local_media"] += 1
        else:
            imported[identifier] = source
    groups = _groups(imported)
    clip_index = _index(load_jsonl(clips), "clip_id")
    reviewed = defaultdict(list)
    for annotation in latest_annotations(annotations):
        if annotation["annotation_status"] != "reviewed":
            excluded["unreviewed_or_rejected_annotations"] += 1
            continue
        clip = clip_index.get(annotation["clip_id"])
        source = imported.get(annotation["source_id"])
        if annotation.get("example_only") or (clip and clip.get("example_only")):
            excluded["synthetic_annotations"] += 1
            continue
        if source is None or clip is None:
            excluded["annotations_without_acquired_source_or_clip"] += 1
            continue
        validate_annotation(annotation, clip)
        if clip.get("source_sha256") != source["sha256"]:
            raise ValueError(f"Source hash mismatch for clip {clip['clip_id']}")
        if not _media_exists(clip.get("video_path"), clips):
            excluded["annotations_without_local_clip"] += 1
            continue
        boss = canonical(annotation["boss"])
        if not boss or ";" in boss or boss.startswith("MULTI_"):
            excluded["annotations_without_specific_boss"] += 1
            continue
        phase = _known(annotation.get("boss_phase")) or "UNREVIEWED"
        keys.append((boss, phase))
        reviewed[(boss, phase, clip["clip_id"])].append(annotation)

    aggregate = defaultdict(lambda: {"sources": set(), "players": set(), "cameras": set(),
                                     "clips": set(), "attacks": set(), "positives": set(),
                                     "negatives": set(), "intervals": defaultdict(list)})
    for (boss, phase, clip_id), labels in reviewed.items():
        clip = clip_index[clip_id]
        source = imported[clip["source_id"]]
        group = groups[clip["source_id"]]
        start, end = clip.get("source_start_ms"), clip.get("source_end_ms")
        if (isinstance(start, bool) or isinstance(end, bool)
                or not isinstance(start, (int, float)) or not isinstance(end, (int, float))
                or not math.isfinite(start) or not math.isfinite(end) or end <= start):
            raise ValueError(f"Clip {clip_id} lacks a valid original PTS interval")
        is_attack = lambda row: row["scope"] == "EVENT" and (
            row["state"] in ATTACK_STATES or row.get("windup_start") is not None
            or row.get("active_start") is not None or row["attack_type"] != "UNKNOWN_ATTACK")
        attacks = [label for label in labels if is_attack(label)]
        negatives = [label for label in labels if label["scope"] == "ENTIRE_CLIP_NON_THREAT"]
        if attacks and negatives:
            raise ValueError(f"Clip {clip_id} has conflicting attack and entire-clip negative reviews")
        stats = aggregate[(boss, phase)]
        # The same original frame extent in duplicate uploads is one clip.
        token = (group, clip["start_source_frame"], clip["end_source_frame"])
        stats["clips"].add(token)
        stats["sources"].add(group)
        if attacks:
            stats["attacks"].add(token)
        if any(label.get("threat_label") is True for label in attacks):
            stats["positives"].add(token)
        if negatives:
            stats["negatives"].add(token)
        player = _known(source.get("player_id")) or _known(source.get("creator"))
        if player:
            stats["players"].add(player.casefold())
        for label in labels:
            # Optional explicit reviewed tags only; do not infer conditions from
            # motion proposals, FPS, the uploader, or a boss/arena name.
            tags = label.get("camera_conditions", [])
            if isinstance(tags, str):
                tags = [tags]
            if not isinstance(tags, list):
                raise ValueError("Reviewed camera_conditions must be a string or list of strings")
            stats["cameras"].update(_known(tag).casefold() for tag in tags if isinstance(tag, str) and _known(tag))
        stats["intervals"][group].append((float(start), float(end)))

    output = []
    for boss, phase in dict.fromkeys(keys):
        stats = aggregate[(boss, phase)]
        count = lambda field: len(stats[field])
        positives, negatives = count("positives"), count("negatives")
        score = _score(count("sources"), positives, negatives, count("players"), count("cameras"))
        status = ("EMPTY" if not stats["clips"] else "POOR" if not positives or not negatives or score < 40
                  else "USABLE" if score < 65 else "GOOD" if score < 85 else "STRONG")
        output.append(dict(zip(COLUMNS, (
            boss, phase, count("sources"),
            round(sum(_union_ms(values) for values in stats["intervals"].values()) / 60000, 6),
            count("attacks"), positives, negatives, count("players"), count("cameras"), score, status,
            f"{references[boss]} boss-specific catalog references (not acquired coverage); "
            f"{count('clips')} distinct real reviewed clip contexts. Minutes are unioned context PTS, "
            "not fully labeled frame minutes. Score is an operational inventory index, not model quality."
        ))))
    summary = {"rows": len(output), "imported_real_sources": len(imported),
               "duplicate_collapsed_sources": len(set(groups.values())),
               "reviewed_clip_phase_groups": len(reviewed), "excluded": dict(excluded),
               "missing_inputs": [str(path) for path in (catalog, sources, clips, annotations) if not path.exists()],
               "score_formula": "20*(min(sources/3,1)+min(positive/50,1)+min(negative/50,1)+min(players/3,1)+min(reviewed_camera_tags/3,1))"}
    return output, summary


def update_coverage(*, output: Path, **kwargs) -> dict:
    rows, summary = build_coverage(**kwargs)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(output)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--catalog", type=Path, default=Path("Dataset/catalog/video_sources.csv"))
    parser.add_argument("--sources", type=Path, default=Path("data/source_manifest.jsonl"))
    parser.add_argument("--clips", type=Path, default=Path("data/clips.jsonl"))
    parser.add_argument("--annotations", type=Path, default=Path("data/annotations.jsonl"))
    parser.add_argument("--taxonomy", type=Path, default=Path("Dataset/catalog/boss_taxonomy.json"))
    parser.add_argument("--existing", type=Path, default=Path("Dataset/boss_coverage.csv"))
    parser.add_argument("--output", type=Path, default=Path("Dataset/boss_coverage.csv"))
    args = parser.parse_args()
    print(json.dumps(update_coverage(**vars(args)), ensure_ascii=False))


if __name__ == "__main__":
    main()
