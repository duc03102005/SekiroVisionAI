# Milestone 0 local validation record

Date: 2026-09-10. Environment: Linux project-preparation workspace; no Windows game execution. The table below preserves the initial local-setup results. Publication was subsequently verified; see the [M0 PASS record](../../milestones/milestone-0.md).

## Observed results

| Check | Actual result |
|---|---|
| Authenticated GitHub profile/listing | Succeeded for duc03102005 |
| Intended repository lookup | HTTP 404 |
| Context7 ONNX resolve/query | Succeeded |
| Context7 Windows App SDK query | Succeeded; exact WGC API selection still requires SDK verification |
| Required custom skills | 6/6 present |
| All installed skill entrypoints | 8/8 pass bundled skill-creator quick_validate.py |
| Reviewed skill resource inventory | 18 files, hashes match review manifest |
| Project validator | SKILL_VALIDATION: PASS |
| Isolated negative gate checks | 5/5 detected expected failures |
| Imported executable files | 0 |
| Application engine directories/code | Absent |
| Remote initial publication | Not performed; destination unavailable |

The five temporary negative cases were a modified reviewed SKILL.md, a deleted referenced document, an added inert .ps1 file, a premature CaptureEngine directory, and a removed upstream LICENSE. Each was rejected in a disposable copy; none modified the real checkout. No negative fixture script was executed.

The validation script only reads and checks artifacts. Its local-link count can change when documentation is added; the release checkpoint's exact output is included in the delivered setup report. The hash inventory covers installed skills, not a claim that external websites will remain unchanged.

Manual suspicious-pattern review found only expected explanatory input/token/security text, including an explicitly labeled bad Process.Start example in the imported security reference. That example was not executed. No imported script or installer ran.

The independent 12-row shadow-decision exercise is documented separately in [decision-outcomes.md](decision-outcomes.md). Do not conflate its virtual decisions with executable engine tests.

## Reproduce local checks

From the project root with Python 3.10+:

```text
python tools/validate_skills.py
git status --porcelain
git log -1 --format=fuller
```

The bundled creator validator used during preparation lives in the task environment, not this repository. It is not a new runtime/project dependency. The project validator requires only Python's standard library and does not install anything.

## Gate interpretation

The initial local checks alone did not pass M0. Commit 15927a071fe415eda7920e118d954c0c8a5e864a was subsequently published to main, all 33 files were verified against the original Git tree, and the clean fetched checkout passed validation. **MILESTONE 0 now PASS.** Capture FPS, latency, inference accuracy and Windows input behavior remain unmeasured.
