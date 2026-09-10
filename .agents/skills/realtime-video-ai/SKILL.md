---
name: realtime-video-ai
description: Develop and evaluate causal temporal vision models for SekiroVisionAI attack phases, threat probability and uncertain time-to-impact from streaming frame sequences.
---

# Realtime temporal video AI

Use vision from a causal history ending at the current observation. Single-frame recognition cannot be the complete attack trigger. Read [model-evaluation.md](references/model-evaluation.md) when selecting, training or comparing models.

## Define a streaming contract first

- Feed timestamps and validity masks with images/features. Start experiments at 320×320 and 416×416, windows of 8/16/24/32 frames, batch 1. Distinguish temporal length T from batch B.
- Record sampling interval in milliseconds. Thirty-two samples at 60 FPS cover approximately 517 ms from first to last; at 120 FPS they cover approximately 258 ms. Changing rate without retraining/validation changes the task.
- Require causality in temporal convolutions, attention, optical flow, padding, normalization and feature construction. Offline interpolation, centered windows or smoothed tracks using future frames are not live inputs.
- Prefer a bounded feature history, track state and compact ROIs over rerunning a large encoder on 32 full frames for every prediction. Measure the whole design before choosing it.
- Reset history on game/capture restart, resize transform change, camera cut, track-identity change and long timestamp gaps. Define a warmup mask; insufficient history means uncertain output and no automatic action.

## Compare architectures without preselecting a winner

Compare causal TCN and small GRU/LSTM heads over vision features as latency-oriented baselines. Compare a causal temporal transformer/lightweight video transformer only with identical data splits and live schedules. Treat VideoMAE as a possible pretraining/teacher or adapted backbone; its offline clip recipe is not evidence of a causal low-latency runtime.

Evaluate RGB/ROI features, pose sequences, weapon tracks and optical flow as separate ablations. Camera motion, lock-on changes and occlusion can dominate optical flow or break pose estimates. Include masks, target-relative motion and camera-motion estimates; missing weapons/keypoints must not become zero-confidence “safe” detections.

Compare detector candidates (including YOLO and RT-DETR), tracker-assisted sparse detection, segmentation and pose only against their contribution to threat/TTI quality per millisecond. A generic pretrained detector is not assumed to recognize Wolf, Sekiro bosses or small weapons. Check weights/data licenses and available ONNX operators before training commitments.

## Output semantics

Produce `attack_class`, `attack_probability`, `time_to_impact_ms`, `attack_direction`, `threat_probability`, and `confidence`, plus phase, track/threat identity, source timestamp, prediction timestamp, validity masks, TTI interval and model/preprocessing version.

- `attack_probability`: calibrated probability of the specified attack event over a stated horizon, not necessarily imminent contact with Wolf.
- `threat_probability`: calibrated probability that this attack intersects Wolf's estimated region within the horizon under a stated motion assumption.
- `time_to_impact_ms`: time from the source observation to the estimated contact event. Return null/invalid when contact is not observable or supported. A point estimate without uncertainty is insufficient for automatic scheduling.
- `attack_direction`: weapon/attack motion in a named coordinate frame. It is not the same as dodge direction or a W/A/S/D key.
- `confidence`: a documented quality/calibration quantity. Do not invent a probability by multiplying correlated detector, tracker and classifier scores.

Keep movement state, attack phase and attack type as separate labels. This allows an enemy to move during a windup or a combo. Use the dataset skill for phase boundaries, contact evidence and source splits.

## Training and deployment checks

Train phase/type, threat and TTI heads only where their labels are valid. Do not set TTI=0 for no-hit footage or regress a fabricated impact from a successful dodge. Keep observed-contact labels separate from uncertain projected-contact estimates. Consider interval or survival-style targets for censored contact.

Export a minimal streaming model early. Validate eager/exported outputs with representative sequences, hidden-state continuity and tolerances before full training. Specify tensor layout, normalization, opset, dynamic/static dimensions, state IO and supported execution providers. Do not assume ONNX export automatically provides GPU support for every operator.

Tune thresholds/calibration on validation data only. Prioritize false Dodge rate and event precision, then recall, consistency and latency. Run a same-boss held-out-source test and a distinct unseen-enemy test; report both. Freeze splits and thresholds before the final test.

The ThreatEngine owns action decisions. This model never sends input directly.

## Primary references

- [TCN sequence-modeling paper](https://arxiv.org/abs/1803.01271)
- [MMAction2](https://github.com/open-mmlab/mmaction2)
- [VideoMAE implementation](https://github.com/MCG-NJU/VideoMAE)

Use those for model research, not claimed Sekiro accuracy. Record exact repository/model revisions and verify maintained export/runtime documentation at implementation time.
