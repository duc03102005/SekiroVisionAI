"""Freeze source/player/session/duplicate-disjoint splits, including held-out bosses."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random

from DatasetTools.common import load_jsonl, sha256_file, utc_now, write_json

GROUP_KEYS = ("source_group_id", "player_id", "creator", "session_id", "duplicate_group_id", "sha256", "url")


def source_components(sources: list[dict]) -> list[list[str]]:
    identifiers = [row["source_id"] for row in sources]
    if len(set(identifiers)) != len(identifiers):
        raise ValueError("Source manifest contains duplicate source IDs.")
    parent = {identifier: identifier for identifier in identifiers}

    def find(value: str) -> str:
        while parent[value] != value:
            parent[value] = parent[parent[value]]
            value = parent[value]
        return value

    def union(a: str, b: str) -> None:
        parent[find(a)] = find(b)

    seen: dict[tuple[str, str], str] = {}
    for row in sources:
        for field in GROUP_KEYS:
            value = str(row.get(field) or "").strip()
            if not value or value.upper() in ("UNKNOWN", "N/A", "UNREVIEWED"):
                continue
            # Creator/player refer to the same grouping concept. IDs are
            # normalized case-insensitively; URLs/hashes are already canonical.
            domain = "player" if field in ("creator", "player_id") else field
            key = (domain, value.casefold())
            if key in seen:
                union(row["source_id"], seen[key])
            else:
                seen[key] = row["source_id"]
    components: dict[str, list[str]] = {}
    for identifier in identifiers:
        components.setdefault(find(identifier), []).append(identifier)
    return sorted((sorted(group) for group in components.values()), key=lambda group: group[0])


def bosses_in(source: dict) -> set[str]:
    bosses = source.get("bosses")
    if isinstance(bosses, list):
        return {str(boss).strip() for boss in bosses if str(boss).strip()}
    return {boss.strip() for boss in str(source.get("boss", "UNKNOWN")).split(";") if boss.strip()}


def partition_sources(sources: list[dict], *, seed: int = 2026, val_fraction: float = 0.15,
                      test_fraction: float = 0.15, held_out_bosses: set[str] | None = None) -> dict:
    if not 0 <= val_fraction < 1 or not 0 <= test_fraction < 1 or val_fraction + test_fraction >= 1:
        raise ValueError("Validation/test fractions must leave a nonzero training fraction.")
    held_out_bosses = held_out_bosses or set()
    by_id = {row["source_id"]: row for row in sources}
    components = source_components(sources)
    held = [component for component in components
            if any(bosses_in(by_id[identifier]) & held_out_bosses for identifier in component)]
    remaining = [component for component in components if component not in held]
    random.Random(seed).shuffle(remaining)
    count = len(remaining)
    val_count = max(1, round(count * val_fraction)) if count >= 3 and val_fraction else 0
    test_count = max(1, round(count * test_fraction)) if count >= 3 and test_fraction else 0
    while val_count + test_count >= count and count:
        if test_count >= val_count and test_count:
            test_count -= 1
        elif val_count:
            val_count -= 1
    validation = remaining[:val_count]
    normal_test = remaining[val_count:val_count + test_count]
    train = remaining[val_count + test_count:]

    def flatten(groups):
        return sorted(identifier for group in groups for identifier in group)

    split = {"train": flatten(train), "val": flatten(validation), "test": flatten(normal_test + held),
             "unseen_source_test": flatten(normal_test), "heldout_boss_test": flatten(held),
             "held_out_bosses": sorted(held_out_bosses), "seed": seed,
             "group_fields": list(GROUP_KEYS), "components": components,
             "warnings": []}
    if not split["val"]:
        split["warnings"].append("No independent validation component; calibration/generalization is unmeasured.")
    if not split["unseen_source_test"]:
        split["warnings"].append("No same-boss/different-source test component.")
    if held_out_bosses and not held:
        split["warnings"].append("Requested held-out bosses have no acquired sources.")
    for row in sources:
        if not row.get("player_id") and not row.get("creator"):
            split["warnings"].append(f"Creator/player unknown for {row['source_id']}; unseen-player claims are unsupported.")
    validate_splits(sources, split)
    return split


def validate_splits(sources: list[dict], split: dict) -> None:
    assignments: dict[str, str] = {}
    for name in ("train", "val", "test"):
        for identifier in split.get(name, []):
            if identifier in assignments:
                raise ValueError(f"Source leakage: {identifier} appears in {assignments[identifier]} and {name}.")
            assignments[identifier] = name
    expected = {row["source_id"] for row in sources}
    if set(assignments) != expected:
        raise ValueError("Every acquired source must be assigned exactly once; unknown source IDs are forbidden.")
    for component in source_components(sources):
        if len({assignments[identifier] for identifier in component}) != 1:
            raise ValueError(f"Creator/session/duplicate leakage across splits: {component}")
    held_bosses = set(split.get("held_out_bosses", []))
    for source in sources:
        if bosses_in(source) & held_bosses and assignments[source["source_id"]] != "test":
            raise ValueError("Held-out boss appeared in training or validation.")
    normal_test = set(split.get("unseen_source_test", []))
    held_test = set(split.get("heldout_boss_test", []))
    if normal_test & held_test or not (normal_test | held_test) <= set(split["test"]):
        raise ValueError("Generalization reports must be disjoint subsets of the test set.")


def write_splits(sources_path: Path, output_dir: Path, **kwargs) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    if any(output_dir.glob("*_sources.txt")) or (output_dir / "split_manifest.json").exists():
        raise ValueError("Split versions are immutable; choose a new output directory.")
    sources = load_jsonl(sources_path)
    result = partition_sources(sources, **kwargs)
    for name in ("train", "val", "test", "unseen_source_test", "heldout_boss_test"):
        path = output_dir / f"{name}_sources.txt"
        path.write_text("".join(identifier + "\n" for identifier in result[name]), encoding="utf-8")
    result.update({"schema_version": "2.0", "created_at": utc_now(),
                   "sources_sha256": sha256_file(sources_path),
                   "source_count": len(sources),
                   "split_sha256": hashlib.sha256(json.dumps({name: result[name] for name in ("train", "val", "test")},
                                                              sort_keys=True).encode()).hexdigest()})
    write_json(output_dir / "split_manifest.json", result)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=Path("data/source_manifest.jsonl"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--held-out-boss", action="append", default=[])
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--val-fraction", type=float, default=0.15)
    parser.add_argument("--test-fraction", type=float, default=0.15)
    args = parser.parse_args()
    print(json.dumps(write_splits(args.sources, args.output, seed=args.seed,
                                  val_fraction=args.val_fraction, test_fraction=args.test_fraction,
                                  held_out_bosses=set(args.held_out_boss))))


if __name__ == "__main__":
    main()
