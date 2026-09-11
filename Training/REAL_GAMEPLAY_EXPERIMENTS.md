# Reproducible real gameplay experiments

These jobs learn from acquired Sekiro video. They are development experiments;
the current reviewed cores do not establish body-contact TTI, safe Dodge timing,
all-boss accuracy, independent human labels or production eligibility.

Use Python3.12, the pinned training/dataset requirements, FFmpeg and ffprobe.
Run from the repository root. This tooling does not run on the user's Windows
application; it produces ONNX artifacts for native evaluation.

## Six-source motion and phase job

Acquire the pinned publisher-provided clips, with source permission and SHA
verification handled by the catalog importer:

```bash
python -m DatasetTools.downloader.acquire_catalog --source-id LIVE_YT_GAMING_SEKIRO_005 --source-id LIVE_YT_GAMING_SEKIRO_016 --source-id LIVE_YT_GAMING_SEKIRO_019 --source-id LIVE_YT_GAMING_SEKIRO_023 --source-id LIVE_YT_GAMING_SEKIRO_026 --source-id LIVE_YT_GAMING_SEKIRO_029
python -m Training.trainers.real_phase_seed data/source_manifest.jsonl artifacts/real-phase-seed --pretrain-epochs 2 --phase-epochs 3
```

The single job writes its exact subprocess arguments, starting source/code
provenance, causal normalized clips, source-group split, checkpoints, ONNX
exports and numerical-parity evidence. Artifact upload should select model
bundles and JSON evidence; source videos and normalized clips are not needed in
the Windows package.

The normalized unlabeled dataset contains175 T16 examples at30Hz. The reviewed
phase dataset contains242 anchors:7 windup,3 active and232 non-attack negatives.
The NPC/traversal clip's locomotion subclass is unknown and stays masked. Source
frames113–116 between the accepted attack phase cores remain unlabeled.

`combat-review-intervals-v1.jsonl` records AI visual consensus/review. Its
annotations do not claim exact attack-onset boundaries. Spark or blood onset,
HP loss and successful deflection never supply a point body-contact label.

Both video IDs used for supervised learning belong to the same conservatively
grouped original-player/session provenance. Consequently, validation/test sets
remain empty. The phase model can report a trained attack head; threat,
body-contact TTI, attack direction and Auto Dodge remain explicitly unsupported.

## Temporal-order learning from selected boss scenes

The next actual representation experiment adds clean Gyoubu/Isshin scene
intervals and a masked same-clip temporal-reversal objective:

```bash
python -m DatasetTools.downloader.acquire_catalog --source-id MARKOV_SEKIRO_A7ECDCA8
python -m Training.datasets.prepare_unlabeled data/source_manifest.jsonl data/temporal-motion-boss-seed-v3 --selections Training/configs/real-motion-selections-v3.jsonl --frames 16 --stride 8
python -m Training.trainers.pretrain data/temporal-motion-boss-seed-v3/unlabeled_samples.jsonl artifacts/live-yt-markov-motion-pretrain-v3 --architecture optical_flow_fusion --frames 16 --size 320 --epochs 2 --batch-size 4 --device cpu --temporal-weight 0.25
python -m Training.export.onnx_bundle artifacts/live-yt-markov-motion-pretrain-v3
```

The observed run uses328 histories from7 video files and164 optimizer steps.
The source's incidental desktop-overlay interval940–1060s is excluded. The
preparation helper checks its source-adjacent exclusion/candidate metadata;
unreviewed portions of a long recording are never selected implicitly.
The two publisher/player groups are conservative grouping identities; actual
player identity is unknown. Pretraining exposure is not a held-out boss test.

`combat-review-intervals-v2.jsonl` adds small Gyoubu certain-phase/negative cores
for development evaluation. Those guarded/occluded attacks still provide no
point body-contact TTI. They must not be used to claim a timing-validated Dodge.

The frozen LIVE-only phase model was evaluated on29 such Gyoubu anchors with no
source overlap in its supervised training or pretraining. At the unchanged0.85
threshold it missed all8 positive anchors and rejected all21 negatives. These
are normalized-frame anchors, including repeated floor-PTS observations; they
are not8 independent attacks. This failed transfer result is retained in
`Evaluation/reports/real-phase-and-motion-2026-09-10.json`. It demonstrates why
this small phase experiment cannot be installed as a production Dodge model.
