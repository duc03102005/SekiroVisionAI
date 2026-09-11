# Temporal AI integration scope — 2026-09-10

The latest user instruction replaces heuristic-first development with parallel
capture quality, multi-source/multi-boss collection, clip mining, annotation,
three temporal training baselines and native model integration. All milestone
PASS/FAIL development gates remain removed. Existing reviewed Agent Skills are
unchanged technical references.

## Implemented work

- Capture retains its full-resolution owned D3D11 source texture. An independent
  color CPU readback preserves aspect and native detail up to1280×720. The UI
  renders that color image;256×144 grayscale is only the optional CV derivative.
- Source catalog:60 unique research sources, including54 YouTube videos verified
  against metadata;15 main bosses,3 Inner bosses and14 minibosses have coverage
  rows. Candidate URLs do not count as acquired minutes or labeled clips.
- Dataset tools: provenance/hash/PTS ingestion, camera-compensated clip proposals,
  short clips, frame mapping, annotation journal/desktop app, validation, grouped
  splits and reviewed sample export. Negatives/proposals are never auto-accepted.
- Training: CNN+GRU, explicit appearance/difference-feature TCN, small causal video
  transformer; masked multi-task objectives, observed-contact-only point TTI,
  source/creator/session/duplicate grouping, held-out boss reporting, calibration,
  self-supervised pretraining, review queues and versioned ONNX export.
- Native ORT runtime: RGB ROI history, fixed model cadence, bounded latest-frame
  processing, shape/name/metadata validation, actual providers reported, missing
  or unsupported heads visible. Python is absent from the Windows executable.
- Model decisions: attack/threat thresholds, quiet-before-arming, hysteresis,
  source-time dwell, consumed strike, cooldown, source-age rejection, TTI window
  and an immediate dispatch-time deadline check. Sweeps/AOE abstain in this first
  policy. Selected direction is user configured, not a learned safe direction.
- Recording: opt-in WIC JPEG frame bundles with1s before/after Dodge or review
  markers, QPC/source-frame metadata, bounded memory/jobs/session disk usage and
  explicit truncation. All markers remain UNREVIEWED.

## Evidence boundary

**No gameplay footage was acquired and no multi-boss gameplay model was trained.**
The source investigation found manual-gated raw data, encrypted/unspecified
datasets, reference-only video sources and a creator reuse offer without a
verified authorized download route. Details are in
[source_research.md](../Dataset/catalog/source_research.md). This is a media
access/usage limitation, not a dataset-completeness or benchmark milestone gate.

Synthetic optimizer runs, ONNX parity, native fixtures and generated-video
roundtrips validate implementation plumbing only. They cannot measure boss
recognition, real false Dodge rate, TTI accuracy or live RTX3070 latency. Their
metadata disables model Auto Dodge. A trained attack-only model may load for
scores without activating unsupported threat/TTI heads.

The requested outcome of reliable AI-triggered Dodge in the user's Sekiro game
is therefore **not yet achieved**. The runnable application retains the explicit
CV fallback and real Shift path while the data/model path is now implemented.

## Remaining concrete work

Acquire reusable actual gameplay; review a small diverse set of attacks,
contact-supported threat/TTI and hard negatives; run supervised baselines; load
the first supported export immediately. The code does not require complete boss
coverage to do this. Capture on the user's Windows/RTX3070, game input reception,
accuracy and real Dodge success require that actual environment. Detector-based
enemy/Wolf tracking, safe-direction learning, CUDA/D3D interop, TensorRT and
calibrated TTI intervals remain further work. Manual ROI is still required.
