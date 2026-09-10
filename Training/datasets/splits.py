"""Transitive source/player/session/duplicate grouping, never frame splitting."""
import argparse
import hashlib
import json
from pathlib import Path
import random

GROUP_FIELDS = ("source_id", "source_group_id", "player_id", "creator", "session_id", "duplicate_group_id", "source_sha256", "source_url", "url")


def read_jsonl(path):
    return [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]


def grouped_sources(rows):
    parents = {row["source_id"]: row["source_id"] for row in rows}
    def root(value):
        while parents[value] != value:
            parents[value] = parents[parents[value]]
            value = parents[value]
        return value
    keys = {}
    for row in rows:
        source = row["source_id"]
        for field in GROUP_FIELDS:
            value = row.get(field)
            if value is None or str(value).strip().lower() in ("", "unknown", "none", "n/a"):
                continue
            domain = "player" if field in ("player_id", "creator") else "url" if field in ("source_url", "url") else field
            key = (domain, str(value).strip().casefold())
            if key in keys:
                parents[root(source)] = root(keys[key])
            keys[key] = source
    groups = {}
    for source in parents:
        groups.setdefault(root(source), set()).add(source)
    return list(groups.values())


def make_splits(rows, seed=17, holdout_bosses=()):
    groups = grouped_sources(rows)
    random.Random(seed).shuffle(groups)
    held = {row["source_id"] for row in rows if row.get("boss") in holdout_bosses}
    result = {name: set() for name in ("train", "val", "test", "heldout_boss")}
    available = []
    for group in groups:
        if group & held:
            result["heldout_boss"].update(group)
        else:
            available.append(group)
    # With fewer than three independent groups, report absent splits. Never
    # manufacture independence by moving clips from one source into two sets.
    n_val = max(1, round(len(available)*0.15)) if len(available) >= 3 else 0
    n_test = max(1, round(len(available)*0.15)) if len(available) >= 3 else 0
    for index, group in enumerate(available):
        split = "val" if index < n_val else "test" if index < n_val+n_test else "train"
        result[split].update(group)
    return result


def validate_splits(rows, splits):
    assigned = {}
    for split, sources in splits.items():
        for source in sources:
            if source in assigned:
                raise ValueError(f"Source leakage: {source}: {assigned[source]} / {split}")
            assigned[source] = split
    for group in grouped_sources(rows):
        names = {assigned[source] for source in group if source in assigned}
        if len(names) > 1:
            raise ValueError(f"Player/session/duplicate leakage: {sorted(group)}")
    missing = {row["source_id"] for row in rows} - set(assigned)
    if missing:
        raise ValueError(f"Reviewed sample sources missing from frozen split: {sorted(missing)}")
    # Canonical DatasetTools splits may also contain acquired sources with no
    # reviewed samples yet. They retain an assignment without inventing rows.


def load_splits(directory):
    splits = {name: set((Path(directory)/f"{name}_sources.txt").read_text(encoding="utf-8").split())
            for name in ("train", "val", "test", "heldout_boss")
            if (Path(directory)/f"{name}_sources.txt").exists()}
    canonical_holdout = Path(directory)/"heldout_boss_test_sources.txt"
    if canonical_holdout.exists():
        heldout = set(canonical_holdout.read_text(encoding="utf-8").split())
        if not heldout <= splits.get("test", set()):
            raise ValueError("Canonical heldout-boss sources must be a subset of test_sources")
        splits["heldout_boss"] = heldout
        splits["test"] -= heldout
    return splits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--holdout-boss", action="append", default=[])
    args = parser.parse_args()
    rows = read_jsonl(args.manifest)
    splits = make_splits(rows, args.seed, args.holdout_boss)
    validate_splits(rows, splits)
    args.output.mkdir(parents=True, exist_ok=False)
    for name, sources in splits.items():
        (args.output/f"{name}_sources.txt").write_text("".join(s+"\n" for s in sorted(sources)), encoding="utf-8")
    audit = {"seed": args.seed, "heldout_bosses": args.holdout_boss,
             "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
             "source_counts": {name: len(sources) for name, sources in splits.items()},
             "group_fields": GROUP_FIELDS}
    (args.output/"split_audit.json").write_text(json.dumps(audit, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(audit, indent=2))


if __name__ == "__main__":
    main()
