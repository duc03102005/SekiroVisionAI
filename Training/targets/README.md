# Native actor role detector

`TargetDetection` loads a real ONNX model that predicts visible Wolf and enemy
body boxes from the color game image. It does not predict attack, TTI or Dodge;
those still require the shared temporal combat pipeline. The native runtime
validates metadata, tensor names/types/shapes and output ranges, performs NMS,
and attaches the original capture sequence/generation/source time. The tracker
then confirms current semantic observations and creates the combat ROI.

## Reviewed gameplay labels

`Dataset/annotations/target-role-review-v1.jsonl` contains 46 visually inspected
original frames from six acquired LIVE-YT-Gaming clips. Five frames mask both
roles as unresolved; 41 usable frames contain 38 Wolf and 26 enemy body boxes.
The observations include ordinary enemies, traversal, friendly NPC dialogue,
foliage, sparks and occlusion. Unknown roles are masked, never negative labels.
These are **AI visual reviews, not independent human gold annotations**.

Every row retains source hash, original frame index/PTS, group, resolution,
review method and normalized visible-body boxes. Weapons are excluded from body
boxes. Mounted enemies include rider and mount as a single combat unit. All six
LIVE clips remain one conservatively grouped source family because original
player/session identities are unknown. No random-frame split is created.

The first frozen source check uses seven separately reviewed Markov frames,
including mounted Gyoubu and an equipment menu. Its source family does not
overlap LIVE; original player independence is unknown. After its failure
informed subsequent architecture choice, those same frames were explicitly
allocated to **development validation** in
`target-role-development-v1.jsonl`. They cannot serve as a final test for later
model choices. The original check and its labels remain preserved as evidence.

## Training and inference contracts

The compact detector has 26,802 parameters and was actually optimized on these
gameplay labels. A second architecture transfers official TorchVision
MobileNetV3-Small ImageNet features into a newly trained dense role head. The
ImageNet classifier is discarded; no generic natural-image label is renamed
Wolf or enemy. See `TargetDetection/pretrained-provenance.json` and its license
notice for the exact checkpoint, hash and applicable usage limits.

Both architectures expose the same native contract:

| Field | Contract |
|---|---|
| Input | `frame`, float `[1,3,192,320]` |
| Preprocess | Full color image, RGB / 255, planar NCHW, half-pixel bilinear resize |
| Scores | `scores`, float `[1,2,24,40]`, role order Wolf, Enemy |
| Boxes | `boxes`, float `[1,2,4,24,40]`, normalized left/top/right/bottom |
| Suppression | Per-role score ≥ 0.75; NMS IoU 0.4; maximum eight boxes per role |
| Validity | Trained semantic metadata is independent of production validation |

The ImageNet variant includes its mean/std normalization inside ONNX, so native
preprocessing remains identical. Scores are uncalibrated. `semantic_supported`
means the role head was trained on those labels; it does not imply generalization
or a production acceptance result. Every current candidate explicitly carries
`production_validated=false`.

Development commands, from the repository root:

```bash
python -m Training.targets.train --output artifacts/targets-run --steps 800
python -m pip install torchvision==0.23.0 --index-url https://download.pytorch.org/whl/cpu --no-deps
python -m pip install Pillow==11.3.0
python -m Training.targets.train --backbone imagenet-mnv3 --output artifacts/targets-mobilenet-run --steps 1400
python -m Training.targets.make_fixture out/testdata/target-fixture.onnx
```

Python is training/testing tooling only. The Windows application loads
`Models/production/targets.onnx` through native ONNX Runtime, alongside the
temporal model, without Python or a user training step.

## Measured first candidate and limits

The 3,400-step compact experiment fits 33/38 Wolf and 26/26 enemy boxes at IoU
≥ 0.5 and score ≥ 0.75, with zero false detections on those reviewed fitting
frames. **These are training-set fit measurements, not held-out accuracy.**
Its separate seven-frame Gyoubu check finds 0/5 Wolf and 0/5 mounted enemy boxes,
with zero false positives. That failure rules out describing this candidate as
a general boss detector and motivates the pretrained-feature experiment.

The completed MobileNetV3-Small FPN experiment uses 1,043,178 parameters and
1,400 real training steps. It fits 38/38 Wolf and 25/26 enemy boxes without false
detections on the reviewed training frames. At the unchanged 0.75 score
threshold, its seven-frame Gyoubu **development validation** finds 1/5 Wolf and
0/5 mounted enemy boxes, with zero false detections. This remains inadequate for
boss deployment. The earlier frozen failure was not erased, and no threshold
was lowered to manufacture detected actors.

Native C++/Python preprocessing and detection/NMS parity also pass for this
pretrained variant on the same three actual source frames. Model-only CPU median
latency was 2.8 ms during fitting evaluation and 4.9 ms during a separate
concurrent development-validation run. These are shared development-host
measurements, not a controlled RTX 3070 comparison or end-to-end game latency.
Detailed evidence is in `Evaluation/reports/target-role-mobilenet-v1-*.json`.

`TargetDetectorTests` ran the actual exported compact model through native ORT.
Three actual original gameplay frames each produced one Wolf plus two enemy
boxes. C++/Python detection and NMS outputs agree within the stated tolerances;
full-color preprocessing differs by at most 1.2e-7. This proves software parity,
not Dodge success. Evidence is in `Evaluation/reports/target-role-real-v2-*.json`.

The CI fixture is input-dependent but explicitly synthetic, untrained and
semantically unsupported; it cannot enable semantic action readiness. Real
model weights, raw frames and training checkpoints stay outside Git. Current
evidence covers neither all bosses nor an RTX 3070 live-game performance test.
