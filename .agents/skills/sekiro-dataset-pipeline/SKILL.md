---
name: sekiro-dataset-pipeline
description: Specify reproducible Sekiro video datasets, semi-automatic attack annotation, uncertain contact labels and source/enemy-disjoint evaluation splits for vision-led Auto Dodge.
---

# Sekiro dataset pipeline

Use [annotation-contract.md](references/annotation-contract.md) and the linked JSON example before writing annotation or training tools. Dataset implementation belongs to M3; M0 establishes the contract only.

## Source registry

Use multiple sources: self-recorded gameplay, permitted public boss fights, no-hit/no-damage runs, full playthroughs, guides, speedruns and challenge runs. Search for Genichiro, Lady Butterfly, Guardian Ape, Great Shinobi Owl, Owl Father, Corrupted/True Monk, Isshin, Demon of Hatred, Ashina Elite, samurai and other minibosses. Vary camera motion, distance, arena, lighting, phase, lock-on, effects and prosthetics.

Record the real source URL or local recording ID, creator, recording session, video hash, retrieval date, permission/license or reference-only status, original FPS/timebase, resolution, codec, edits and duplicate-group ID. A search result is not downloaded media or permission to redistribute it. Start with self-recording when footage rights/access are unresolved; keep public reference links separately. Do not bypass restrictions or ingest unrelated account/session data.

No-hit footage is valuable for motion and successful player behavior but systematically omits many contact events and failures. Include contact, misses, interrupted attacks, blocked/deflected attacks, feints, recovery, camera cuts, menus, traversal and occlusion as distinct evidence. Sample hard negatives and long non-threat periods to measure unwanted Dodge.

## Pipeline specification

1. Ingest/reference sources with a manifest and immutable source hashes.
2. Propose combat sections using scene/shot boundaries and detector/track activity; review the proposals.
3. Extract clips with context before windup and after recovery. Preserve original PTS mappings, decoder version and extraction parameters.
4. Normalize resolution and cadence only with a recorded transform. For variable frame rate, map normalized frames to source timestamps. Mark duplicated/interpolated frames and never treat them as new observations.
5. Propose boss/track identity and attack segments; keep uncertain identities explicit.
6. Present candidate boundaries/keyframes to a human annotator, not a task to label every frame. Propagate accepted labels with tracking/temporal models, then review disagreements, high uncertainty and random samples.
7. Version annotations with author/reviewer, confidence, evidence type and revision history. Keep model-generated proposals separate from accepted labels.
8. Group sources before train/validation/test partitioning. Freeze manifests and verify leakage before training.

Keep temporal augmentations consistent across all frames and labels. Speed changes require rescaling timestamps/TTI; horizontal flips require remapping screen directions and key/camera assumptions. Do not relabel a visually flipped clip as a verified safe dodge direction.

## Annotation design

Separate locomotion, attack phase and attack type. Include all classes required by the brief. Use an attack-event ID plus per-strike IDs for combos; a whole combo can contain several independent threats. Optional memory/hitbox diagnostics can aid labeling but must be separately tagged and absent from vision-only evaluation inputs.

Annotate observed visual contact only when visible. For an attack avoided by the player, the counterfactual impact is generally unknown; use null or an explicitly reviewed interval estimate and a separate loss mask. HP loss, posture changes, dodge start and perilous-symbol appearance are not interchangeable contact labels. A sweep is an attack type, not proof a Dodge can avoid it.

Record player Dodge as an observation: input time only when self-recording input logs exist, otherwise animation onset with uncertainty. A successful runner's action is not a general optimal-control target.

## Splits and acceptance

- Keep all derived clips of one full source in one split, including duplicate uploads and re-encodes. Also group recording session/creator where overlap is plausible.
- Keep validation separate for threshold/calibration selection. Never tune on the held-out test.
- Publish a same-enemy/different-source test and a separate unseen-enemy/family test; list train/test enemy identities. When the dataset is too small, report the missing coverage instead of claiming generalization.
- Report source/event counts by split, attack type, phase, contact evidence and camera condition; unknown labels and negative minutes; reviewer agreement and boundary uncertainty.
- M3 requires a real video → clips → metadata → annotation roundtrip, stable PTS mapping and no source overlap. A JSON example or unexecuted design is not M3 PASS.

## Primary research references

- [MMAction2 data preparation](https://github.com/open-mmlab/mmaction2/tree/main/tools/data)
- [CVAT annotation platform](https://github.com/cvat-ai/cvat)
- [FFmpeg documentation](https://ffmpeg.org/documentation.html)

Select pinned tool versions when implementation starts. Review download helpers and avoid shell-interpolating titles, URLs or filenames. Keep media/model weights outside Git; commit manifests, schemas, split IDs and annotation revisions.
