"""Validate M0 skill artifacts without executing skill instructions or using network.

This deliberately validates the project's simple name/description frontmatter subset,
not arbitrary YAML. It cannot certify a skill's intent or verify a GitHub push.
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "windows-gpu-capture", "realtime-video-ai", "sekiro-dataset-pipeline",
    "low-latency-inference", "combat-decision-engine", "windows-native-app",
    "winui-code-review", "ort-build",
}
APP_DIRS = {
    "App", "CaptureEngine", "VisionEngine", "TemporalEngine", "ThreatEngine",
    "DodgeEngine", "InputEngine", "ModelRuntime", "Overlay", "DatasetTools",
    "Training", "Evaluation", "Models", "Config", "Tests",
}
LINK = re.compile(r"\[[^\]\n]*\]\(([^\s)]+)(?:\s+\"[^\"]*\")?\)")


def check_skills(root: Path) -> tuple[list[str], dict]:
    errors: list[str] = []
    skills = root / ".agents/skills"
    found = {p.parent.name for p in skills.glob("*/SKILL.md")}
    if found != EXPECTED:
        errors.append(f"Skill set mismatch: missing={sorted(EXPECTED-found)}, extra={sorted(found-EXPECTED)}")
    files = sorted(p for p in skills.rglob("*") if p.is_file())
    for p in skills.rglob("*"):
        if p.is_symlink():
            errors.append(f"Symlink in reviewed skill tree: {p.relative_to(root)}")
    for p in files:
        if p.name != "LICENSE" and p.suffix not in {".md", ".json"}:
            errors.append(f"Unreviewed executable/resource type: {p.relative_to(root)}")
    for p in sorted(skills.glob("*/SKILL.md")):
        raw = p.read_text(encoding="utf-8")
        lines = raw.splitlines()
        if not lines or lines[0] != "---" or "---" not in lines[1:]:
            errors.append(f"Missing frontmatter: {p.parent.name}")
            continue
        end = lines.index("---", 1)
        fields = {}
        for line in lines[1:end]:
            key, sep, value = line.partition(":")
            if not sep or key not in {"name", "description"} or key in fields:
                errors.append(f"Unsupported/duplicate frontmatter field in {p.parent.name}: {key}")
            fields[key] = value.strip().strip('"').strip("'")
        if fields.get("name") != p.parent.name:
            errors.append(f"Name/path mismatch: {p.parent.name}")
        if not re.fullmatch(r"[a-z0-9-]{1,63}", fields.get("name", "")):
            errors.append(f"Invalid skill name: {p.parent.name}")
        if len(fields.get("description", "")) < 30:
            errors.append(f"Missing/short description: {p.parent.name}")
        if re.search(r"\b(?:TODO|TBD|FIXME)\b|\[INSERT", raw):
            errors.append(f"Unfinished scaffold: {p.parent.name}")

    link_count = 0
    for p in sorted(root.rglob("*.md")):
        if ".git" in p.parts:
            continue
        for target in LINK.findall(p.read_text(encoding="utf-8")):
            u = urlsplit(target)
            if u.scheme or not u.path:
                continue
            link_count += 1
            dest = (p.parent / unquote(u.path)).resolve()
            if not dest.is_relative_to(root.resolve()) or not dest.exists():
                errors.append(f"Broken/outside local reference: {p.relative_to(root)} -> {target}")

    review_path = root / ".agents/skill-review.json"
    if not review_path.exists():
        errors.append("Missing reviewed file manifest")
    else:
        review = json.loads(review_path.read_text())
        recorded = review.get("files", {})
        actual = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
        if set(recorded) != set(actual):
            errors.append("Reviewed file inventory differs from installed skill files")
        for name, digest in actual.items():
            if recorded.get(name) != digest:
                errors.append(f"Review hash mismatch: {name}")
        decisions = review.get("skills", {})
        if set(decisions) != EXPECTED or any(v.get("status") != "REVIEWED" for v in decisions.values()):
            errors.append("Not every skill has a completed documented review")

    lock = json.loads((root / ".agents/external-skills.lock.json").read_text())
    for entry in lock.get("imports", []):
        if not re.fullmatch(r"[0-9a-f]{40}", entry.get("commit", "")):
            errors.append(f"Unpinned upstream revision: {entry.get('name')}")
        for item in entry.get("files", []):
            p = root / item["local_path"]
            if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest() != item["installed_sha256"]:
                errors.append(f"External import hash mismatch: {item['local_path']}")
        if not (skills / entry["name"] / "LICENSE").is_file():
            errors.append(f"Missing upstream license: {entry['name']}")

    sample = json.loads((skills / "sekiro-dataset-pipeline/references/annotation.example.json").read_text())
    if sample.get("example_only") is not True:
        errors.append("Synthetic annotation example must be marked example_only")
    start, end = sample["clip_start"], sample["clip_end"]
    if not (0 <= start <= sample["anchor_frame"] < end):
        errors.append("Invalid sample clip/anchor interval")
    if not (sample["fps_num"] > 0 and sample["fps_den"] > 0):
        errors.append("Invalid sample frame timebase")
    phases = [sample[k] for k in ("windup_start", "active_start", "recovery_start") if sample[k] is not None]
    if phases != sorted(phases) or any(not start <= v < end for v in phases):
        errors.append("Invalid sample phase order or clip boundaries")
    if sample["impact_evidence"] != "OBSERVED_CONTACT" and sample["impact_frame"] is not None:
        errors.append("Non-observed example contact cannot have a point impact label")
    if sample["would_hit_without_dodge"] == "UNKNOWN" and sample["impact_evidence"] == "ESTIMATED_COUNTERFACTUAL":
        errors.append("Counterfactual example needs an explicit reviewed assumption")

    # This attestation was added only after fetching and verifying the published tree.
    # It is a recorded review, not a fresh online GitHub verification or security boundary.
    publication_path = root / ".agents/milestone-0-publication.json"
    publication_ok = False
    if publication_path.exists():
        publication = json.loads(publication_path.read_text())
        publication_ok = (
            publication.get("status") == "PASS"
            and publication.get("repository") == "duc03102005/SekiroVisionAI"
            and publication.get("remote_ref_and_tree_verified") is True
            and publication.get("local_clean_at_verification") is True
            and re.fullmatch(r"[0-9a-f]{40}", publication.get("commit", "")) is not None
            and re.fullmatch(r"[0-9a-f]{40}", publication.get("tree", "")) is not None
            and publication.get("skill_review_manifest_sha256") == hashlib.sha256(review_path.read_bytes()).hexdigest()
        )
        if not publication_ok:
            errors.append("Invalid or stale M0 publication attestation")
    if not publication_ok:
        for name in APP_DIRS:
            if (root / name).exists():
                errors.append(f"Application directory present before M0 publication: {name}")
    return errors, {"skills": len(found), "reviewed_skill_files": len(files), "local_references": link_count}


def main() -> int:
    try:
        errors, counts = check_skills(ROOT)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"SKILL_VALIDATION: FAIL — {type(exc).__name__}: {exc}")
        return 1
    print(json.dumps(counts, sort_keys=True))
    for error in errors:
        print("ERROR:", error)
    print("SKILL_VALIDATION:", "FAIL" if errors else "PASS")
    print("Publication is recorded separately; this offline check does not reverify GitHub or game performance.")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
